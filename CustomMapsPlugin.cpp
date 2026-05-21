#include "pch.h"
#include "CustomMapsPlugin.h"
#include <fstream>

BAKKESMOD_PLUGIN(CustomMapsPlugin, "Custom Maps Plugin", plugin_version, PLUGINTYPE_FREEPLAY)

void CustomMapsPlugin::onLoad() {
    EnsureFolderExists();
    LoadFavorites();
    LoadMapIndex();
    installedMaps = GetInstalledMaps();

    cvarManager->registerNotifier("custommaps_open", [this](std::vector<std::string> args) {
        cvarManager->executeCommand("togglemenu custommaps");
        }, "Open Custom Maps Plugin", PERMISSION_ALL);
}

void CustomMapsPlugin::onUnload() {
    SaveFavorites();
    previewImage = nullptr;
}

std::filesystem::path CustomMapsPlugin::GetModsFolder() {
    auto tryRegistry = [](HKEY root, const std::string& subKey,
        const std::string& value) -> std::string {
            HKEY key;
            if (RegOpenKeyExA(root, subKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
                return "";
            char buf[512];
            DWORD size = sizeof(buf);
            DWORD type = REG_SZ;
            if (RegQueryValueExA(key, value.c_str(), NULL, &type,
                (LPBYTE)buf, &size) != ERROR_SUCCESS) {
                RegCloseKey(key);
                return "";
            }
            RegCloseKey(key);
            return std::string(buf);
        };

    std::string epicPath = tryRegistry(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\EpicGames\\Unreal Engine\\RocketLeague",
        "InstalledDirectory");
    if (!epicPath.empty()) {
        return std::filesystem::path(epicPath) /
            "TAGame" / "CookedPCConsole" / "mods";
    }

    std::string steamPath = tryRegistry(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 252950",
        "InstallLocation");
    if (!steamPath.empty()) {
        return std::filesystem::path(steamPath) /
            "TAGame" / "CookedPCConsole" / "mods";
    }

    std::vector<std::string> commonPaths = {
        "C:\\Program Files\\Epic Games\\rocketleague",
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\rocketleague",
        "D:\\Program Files\\Epic Games\\rocketleague",
        "D:\\Steam\\steamapps\\common\\rocketleague",
        "C:\\Steam\\steamapps\\common\\rocketleague",
        "E:\\Program Files\\Epic Games\\rocketleague",
        "F:\\Program Files\\Epic Games\\rocketleague",
    };
    for (auto& p : commonPaths) {
        auto rlPath = std::filesystem::path(p) / "TAGame" / "CookedPCConsole";
        if (std::filesystem::exists(rlPath)) {
            return rlPath / "mods";
        }
    }

    return "C:\\Program Files\\Epic Games\\rocketleague\\TAGame\\CookedPCConsole\\mods";
}

std::filesystem::path CustomMapsPlugin::GetImageCacheFolder() {
    auto folder = gameWrapper->GetDataFolder() / "plugins" / "CustomMaps" / "cache";
    if (!std::filesystem::exists(folder)) {
        std::filesystem::create_directories(folder);
    }
    return folder;
}

std::filesystem::path CustomMapsPlugin::GetFavoritesPath() {
    return gameWrapper->GetDataFolder() / "plugins" / "CustomMaps" / "favorites.txt";
}

void CustomMapsPlugin::LoadFavorites() {
    favorites.clear();
    auto path = GetFavoritesPath();
    if (!std::filesystem::exists(path)) return;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) favorites.insert(line);
    }
}

void CustomMapsPlugin::SaveFavorites() {
    auto path = GetFavoritesPath();
    std::ofstream file(path);
    for (auto& fav : favorites) {
        file << fav << "\n";
    }
}

bool CustomMapsPlugin::IsFavorite(const std::string& mapName) {
    return favorites.count(mapName) > 0;
}

void CustomMapsPlugin::ToggleFavorite(const std::string& mapName) {
    if (IsFavorite(mapName)) {
        favorites.erase(mapName);
    }
    else {
        favorites.insert(mapName);
    }
    SaveFavorites();
}

void CustomMapsPlugin::EnsureFolderExists() {
    auto folder = GetModsFolder();
    if (!std::filesystem::exists(folder)) {
        std::filesystem::create_directories(folder);
    }
    auto cacheFolder = gameWrapper->GetDataFolder() / "plugins" / "CustomMaps";
    if (!std::filesystem::exists(cacheFolder)) {
        std::filesystem::create_directories(cacheFolder);
    }
}

void CustomMapsPlugin::LoadMapIndex() {
    maps.clear();
    filteredMaps.clear();
    statusMessage = "Loading map list...";

    std::thread([this]() {
        HINTERNET hInternet = InternetOpenA("CustomMapsPlugin",
            INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hInternet) { statusMessage = "Network error."; return; }

        HINTERNET hUrl = InternetOpenUrlA(hInternet,
            "https://raw.githubusercontent.com/Capstoner1/CustomMapsPlugin/main/CustomMapsPlugin.json",
            NULL, 0, INTERNET_FLAG_RELOAD, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            statusMessage = "Failed to reach map list.";
            return;
        }

        std::string data;
        char buf[4096];
        DWORD bytesRead = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead)
            && bytesRead > 0) {
            data.append(buf, bytesRead);
        }
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        std::vector<MapEntry> loaded;
        size_t pos = 0;
        while ((pos = data.find("\"name\"", pos)) != std::string::npos) {
            MapEntry m;
            auto extract = [&](const std::string& key) -> std::string {
                size_t k = data.find("\"" + key + "\"", pos);
                if (k == std::string::npos) return "";
                k = data.find(":", k) + 1;
                k = data.find("\"", k) + 1;
                size_t end = data.find("\"", k);
                return data.substr(k, end - k);
                };
            m.name = extract("name");
            m.author = extract("author");
            m.description = extract("description");
            m.category = extract("category");
            m.downloadUrl = extract("downloadUrl");
            m.previewUrl = extract("previewUrl");
            m.fileSize = extract("fileSize");
            if (!m.name.empty()) loaded.push_back(m);
            pos++;
        }

        maps = loaded;
        filteredMaps = maps;
        statusMessage = "Loaded " + std::to_string(maps.size()) + " maps.";
        }).detach();
}

void CustomMapsPlugin::LoadPreviewImage(const std::string& url) {
    if (url.empty() || url == currentPreviewUrl || loadingPreview) return;
    currentPreviewUrl = url;
    loadingPreview = true;
    previewImage = nullptr;

    std::thread([this, url]() {
        std::string filename = url.substr(url.find_last_of('/') + 1);
        auto cachePath = GetImageCacheFolder() / filename;

        if (!std::filesystem::exists(cachePath)) {
            HINTERNET hInternet = InternetOpenA("CustomMapsPlugin",
                INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
            if (!hInternet) { loadingPreview = false; return; }

            HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(),
                NULL, 0, INTERNET_FLAG_RELOAD, 0);
            if (!hUrl) {
                InternetCloseHandle(hInternet);
                loadingPreview = false;
                return;
            }

            std::ofstream outFile(cachePath, std::ios::binary);
            char buf[8192];
            DWORD bytesRead = 0;
            while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead)
                && bytesRead > 0) {
                outFile.write(buf, bytesRead);
            }
            outFile.close();
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
        }

        previewImage = std::make_shared<ImageWrapper>(cachePath, false, true);
        loadingPreview = false;
        }).detach();
}

void CustomMapsPlugin::DownloadAndInstallMap(const MapEntry& map) {
    if (isDownloading) return;
    isDownloading = true;
    downloadProgress = 0.0f;
    downloadingMapName = map.name;
    statusMessage = "Downloading: " + map.name + "...";

    std::string url = map.downloadUrl;
    std::string mapName = map.name;
    std::filesystem::path modsFolder = GetModsFolder();

    std::thread([this, url, mapName, modsFolder]() {
        HINTERNET hInternet = InternetOpenA("CustomMapsPlugin",
            INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
        if (!hInternet) {
            statusMessage = "Network error.";
            isDownloading = false;
            return;
        }

        HINTERNET hUrl = InternetOpenUrlA(hInternet, url.c_str(),
            NULL, 0, INTERNET_FLAG_RELOAD, 0);
        if (!hUrl) {
            InternetCloseHandle(hInternet);
            statusMessage = "Failed to download map.";
            isDownloading = false;
            return;
        }

        DWORD totalSize = 0;
        DWORD bufSize = sizeof(DWORD);
        HttpQueryInfo(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
            &totalSize, &bufSize, NULL);

        std::filesystem::path tempZip = modsFolder / (mapName + ".zip");
        std::ofstream outFile(tempZip, std::ios::binary);
        if (!outFile) {
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);
            statusMessage = "Failed to create temp file.";
            isDownloading = false;
            return;
        }

        char buf[8192];
        DWORD bytesRead = 0;
        DWORD totalDownloaded = 0;
        while (InternetReadFile(hUrl, buf, sizeof(buf), &bytesRead)
            && bytesRead > 0) {
            outFile.write(buf, bytesRead);
            totalDownloaded += bytesRead;
            if (totalSize > 0) {
                downloadProgress = (float)totalDownloaded / (float)totalSize;
            }
        }
        outFile.close();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);

        downloadProgress = 1.0f;
        statusMessage = "Extracting: " + mapName + "...";

        std::string zipPath = tempZip.string();
        std::string outPath = modsFolder.string();

        bool extractSuccess = false;
        {
            std::wstring wZipPath(zipPath.begin(), zipPath.end());
            std::wstring wOutPath(outPath.begin(), outPath.end());

            IShellDispatch* pShell = nullptr;
            CoInitialize(NULL);
            HRESULT hr = CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER,
                IID_IShellDispatch, (void**)&pShell);

            if (SUCCEEDED(hr) && pShell) {
                VARIANT vZip, vOut, vOpts;
                VariantInit(&vZip);
                VariantInit(&vOut);
                VariantInit(&vOpts);

                vZip.vt = VT_BSTR;
                vZip.bstrVal = SysAllocString(wZipPath.c_str());
                vOut.vt = VT_BSTR;
                vOut.bstrVal = SysAllocString(wOutPath.c_str());
                vOpts.vt = VT_I4;
                vOpts.lVal = 4 | 16 | 512 | 1024;

                Folder* pZipFolder = nullptr;
                Folder* pOutFolder = nullptr;

                hr = pShell->NameSpace(vZip, &pZipFolder);
                if (SUCCEEDED(hr) && pZipFolder) {
                    hr = pShell->NameSpace(vOut, &pOutFolder);
                    if (SUCCEEDED(hr) && pOutFolder) {
                        FolderItems* pItems = nullptr;
                        pZipFolder->Items(&pItems);
                        if (pItems) {
                            VARIANT vItems;
                            VariantInit(&vItems);
                            vItems.vt = VT_DISPATCH;
                            vItems.pdispVal = pItems;
                            hr = pOutFolder->CopyHere(vItems, vOpts);
                            Sleep(3000);
                            extractSuccess = SUCCEEDED(hr);
                            pItems->Release();
                        }
                        pOutFolder->Release();
                    }
                    pZipFolder->Release();
                }

                SysFreeString(vZip.bstrVal);
                SysFreeString(vOut.bstrVal);
                pShell->Release();
            }
            CoUninitialize();
        }

        std::filesystem::remove(tempZip);
        isDownloading = false;
        downloadProgress = 0.0f;

        if (extractSuccess) {
            installedMaps = GetInstalledMaps();
            statusMessage = "Installed: " + mapName + " — Ready in freeplay!";
        }
        else {
            statusMessage = "Extraction failed. Please try again.";
        }
        }).detach();
}

void CustomMapsPlugin::LaunchMap(const std::filesystem::path& mapPath) {
    pendingMapPath = mapPath.string();
    pendingLaunch = true;
    statusMessage = "Launching: " + mapPath.filename().string();
}

std::vector<MapEntry> CustomMapsPlugin::GetInstalledMaps() {
    std::vector<MapEntry> installed;
    auto folder = GetModsFolder();
    if (!std::filesystem::exists(folder)) return installed;
    for (auto& entry : std::filesystem::directory_iterator(folder)) {
        auto ext = entry.path().extension().string();
        if (ext == ".upk" || ext == ".udk") {
            MapEntry m;
            m.name = entry.path().stem().string();
            m.filePath = entry.path().string();
            installed.push_back(m);
        }
    }
    return installed;
}

void CustomMapsPlugin::Render() {
    if (!windowOpen) return;

    ImGui::SetNextWindowSize(ImVec2(780, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Custom Maps", &windowOpen, ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("tabs")) {

        // ---- BROWSE TAB ----
        if (ImGui::BeginTabItem("Browse")) {
            ImGui::Text("Name:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180);
            bool nameChanged = ImGui::InputText("##search", searchBuf, sizeof(searchBuf));
            ImGui::SameLine();
            ImGui::Text("Author:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(130);
            bool authorChanged = ImGui::InputText("##authorsearch", authorSearchBuf, sizeof(authorSearchBuf));
            ImGui::SameLine();
            bool favChanged = false;
            if (ImGui::Checkbox("Favorites only", &showFavoritesOnly)) {
                favChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                memset(searchBuf, 0, sizeof(searchBuf));
                memset(authorSearchBuf, 0, sizeof(authorSearchBuf));
                showFavoritesOnly = false;
                LoadMapIndex();
            }

            if (nameChanged || authorChanged || favChanged) {
                std::string nameQuery(searchBuf);
                std::string authorQuery(authorSearchBuf);
                std::transform(nameQuery.begin(), nameQuery.end(), nameQuery.begin(), ::tolower);
                std::transform(authorQuery.begin(), authorQuery.end(), authorQuery.begin(), ::tolower);
                filteredMaps.clear();
                for (auto& m : maps) {
                    std::string name = m.name;
                    std::string author = m.author;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    std::transform(author.begin(), author.end(), author.begin(), ::tolower);
                    bool nameMatch = nameQuery.empty() || name.find(nameQuery) != std::string::npos;
                    bool authorMatch = authorQuery.empty() || author.find(authorQuery) != std::string::npos;
                    bool favMatch = !showFavoritesOnly || IsFavorite(m.name);
                    if (nameMatch && authorMatch && favMatch) {
                        filteredMaps.push_back(m);
                    }
                }
                selectedMap = -1;
                lastSelectedMap = -2;
                previewImage = nullptr;
                currentPreviewUrl = "";
            }

            ImGui::Separator();
            ImGui::Columns(2, "browselayout");
            ImGui::SetColumnWidth(0, 260);

            ImGui::Text("Maps (%zu)", filteredMaps.size());
            ImGui::BeginChild("maplist", ImVec2(0, 400), true);
            for (int i = 0; i < (int)filteredMaps.size(); i++) {
                std::string label = (IsFavorite(filteredMaps[i].name) ? "* " : "  ")
                    + filteredMaps[i].name;
                if (ImGui::Selectable(label.c_str(), selectedMap == i)) {
                    selectedMap = i;
                }
                ImGui::TextDisabled("    by %s", filteredMaps[i].author.c_str());
            }
            ImGui::EndChild();

            ImGui::NextColumn();

            if (selectedMap != lastSelectedMap) {
                lastSelectedMap = selectedMap;
                previewImage = nullptr;
                currentPreviewUrl = "";
                if (selectedMap >= 0 && selectedMap < (int)filteredMaps.size()) {
                    LoadPreviewImage(filteredMaps[selectedMap].previewUrl);
                }
            }

            ImGui::Text("Details");
            ImGui::BeginChild("mapdetail", ImVec2(0, 400), true);
            if (selectedMap >= 0 && selectedMap < (int)filteredMaps.size()) {
                auto& m = filteredMaps[selectedMap];

                if (previewImage && previewImage->IsLoadedForImGui()) {
                    ImTextureID texID = previewImage->GetImGuiTex();
                    if (texID) {
                        float availWidth = ImGui::GetContentRegionAvail().x;
                        auto size = previewImage->GetSizeF();
                        float aspectRatio = size.Y / size.X;
                        float imgWidth = availWidth;
                        float imgHeight = imgWidth * aspectRatio;
                        if (imgHeight > 140) {
                            imgHeight = 140;
                            imgWidth = imgHeight / aspectRatio;
                        }
                        ImGui::Image(texID, ImVec2(imgWidth, imgHeight));
                        ImGui::Separator();
                    }
                }
                else if (loadingPreview) {
                    ImGui::TextDisabled("Loading preview...");
                    ImGui::Separator();
                }

                ImGui::TextWrapped("Name: %s", m.name.c_str());
                ImGui::TextWrapped("Author: %s", m.author.c_str());
                if (!m.fileSize.empty()) {
                    ImGui::TextWrapped("Size: %s", m.fileSize.c_str());
                }
                ImGui::Separator();
                ImGui::TextWrapped("%s", m.description.c_str());
                ImGui::Separator();

                bool isFav = IsFavorite(m.name);
                if (isFav) {
                    if (ImGui::Button("Remove from Favorites", ImVec2(-1, 0))) {
                        ToggleFavorite(m.name);
                    }
                }
                else {
                    if (ImGui::Button("Add to Favorites", ImVec2(-1, 0))) {
                        ToggleFavorite(m.name);
                    }
                }
                ImGui::Spacing();

                if (isDownloading && downloadingMapName == m.name) {
                    ImGui::Text("Downloading...");
                    ImGui::ProgressBar(downloadProgress, ImVec2(-1, 0));
                }
                else {
                    if (ImGui::Button("Download & Install", ImVec2(-1, 0))) {
                        DownloadAndInstallMap(m);
                    }
                }
            }
            else {
                ImGui::TextDisabled("Select a map on the left.");
            }
            ImGui::EndChild();
            ImGui::Columns(1);
            ImGui::EndTabItem();
        }

        // ---- FAVORITES TAB ----
        if (ImGui::BeginTabItem("Favorites")) {
            // Search bar
            ImGui::Text("Search:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            static char favSearchBuf[256] = {};
            ImGui::InputText("##favsearch", favSearchBuf, sizeof(favSearchBuf));

            ImGui::Separator();
            ImGui::Columns(2, "favlayout");
            ImGui::SetColumnWidth(0, 260);

            std::string favQuery(favSearchBuf);
            std::transform(favQuery.begin(), favQuery.end(), favQuery.begin(), ::tolower);

            std::vector<MapEntry> favMaps;
            for (auto& m : maps) {
                if (IsFavorite(m.name)) {
                    if (favQuery.empty()) {
                        favMaps.push_back(m);
                    }
                    else {
                        std::string name = m.name;
                        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                        if (name.find(favQuery) != std::string::npos)
                            favMaps.push_back(m);
                    }
                }
            }

            ImGui::Text("Favorite Maps (%zu)", favMaps.size());
            ImGui::BeginChild("favlist", ImVec2(0, 400), true);
            static int selectedFav = -1;
            for (int i = 0; i < (int)favMaps.size(); i++) {
                if (ImGui::Selectable(favMaps[i].name.c_str(), selectedFav == i)) {
                    selectedFav = i;
                }
                ImGui::TextDisabled("  by %s", favMaps[i].author.c_str());
            }
            ImGui::EndChild();

            ImGui::NextColumn();
            ImGui::Text("Details");
            ImGui::BeginChild("favdetail", ImVec2(0, 400), true);
            if (selectedFav >= 0 && selectedFav < (int)favMaps.size()) {
                auto& m = favMaps[selectedFav];
                ImGui::TextWrapped("Name: %s", m.name.c_str());
                ImGui::TextWrapped("Author: %s", m.author.c_str());
                if (!m.fileSize.empty()) {
                    ImGui::TextWrapped("Size: %s", m.fileSize.c_str());
                }
                ImGui::Separator();
                ImGui::TextWrapped("%s", m.description.c_str());
                ImGui::Separator();
                if (ImGui::Button("Remove from Favorites", ImVec2(-1, 0))) {
                    ToggleFavorite(m.name);
                    selectedFav = -1;
                }
                ImGui::Spacing();

                // Find installed path for this map if it exists
                std::filesystem::path installedPath;
                for (auto& inst : installedMaps) {
                    if (inst.name.find(m.name.substr(0, 10)) != std::string::npos
                        || m.name.find(inst.name.substr(0, 10)) != std::string::npos) {
                        installedPath = inst.filePath;
                        break;
                    }
                }

                if (!installedPath.empty()) {
                    if (ImGui::Button("Launch in Freeplay", ImVec2(-1, 0))) {
                        LaunchMap(installedPath);
                    }
                    ImGui::Spacing();
                }

                if (isDownloading && downloadingMapName == m.name) {
                    ImGui::Text("Downloading...");
                    ImGui::ProgressBar(downloadProgress, ImVec2(-1, 0));
                }
                else {
                    if (ImGui::Button("Download & Install", ImVec2(-1, 0))) {
                        DownloadAndInstallMap(m);
                    }
                }
            }
            else {
                ImGui::TextDisabled("Select a map on the left.");
            }
            ImGui::EndChild();
            ImGui::Columns(1);
            ImGui::EndTabItem();
        }

        // ---- INSTALLED TAB ----
        if (ImGui::BeginTabItem("Installed")) {
            // Search bar
            ImGui::Text("Search:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200);
            static char instSearchBuf[256] = {};
            ImGui::InputText("##instsearch", instSearchBuf, sizeof(instSearchBuf));

            ImGui::Separator();
            ImGui::Columns(2, "installedlayout");
            ImGui::SetColumnWidth(0, 280);

            std::string instQuery(instSearchBuf);
            std::transform(instQuery.begin(), instQuery.end(), instQuery.begin(), ::tolower);

            std::vector<MapEntry> filteredInstalled;
            for (auto& m : installedMaps) {
                if (instQuery.empty()) {
                    filteredInstalled.push_back(m);
                }
                else {
                    std::string name = m.name;
                    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                    if (name.find(instQuery) != std::string::npos)
                        filteredInstalled.push_back(m);
                }
            }

            ImGui::Text("Installed Maps (%zu)", filteredInstalled.size());
            ImGui::BeginChild("installedlist", ImVec2(0, 400), true);
            for (int i = 0; i < (int)filteredInstalled.size(); i++) {
                if (ImGui::Selectable(
                    filteredInstalled[i].name.c_str(),
                    selectedInstalled == i)) {
                    selectedInstalled = i;
                }
            }
            ImGui::EndChild();

            ImGui::NextColumn();
            ImGui::Text("Actions");
            ImGui::BeginChild("installeddetail", ImVec2(0, 400), true);
            if (selectedInstalled >= 0
                && selectedInstalled < (int)filteredInstalled.size()) {
                auto& m = filteredInstalled[selectedInstalled];
                ImGui::TextWrapped("%s", m.name.c_str());
                ImGui::Separator();
                if (ImGui::Button("Launch in Freeplay", ImVec2(-1, 0))) {
                    LaunchMap(m.filePath);
                }
                ImGui::Spacing();
                if (ImGui::Button("Delete Map", ImVec2(-1, 0))) {
                    std::filesystem::remove(m.filePath);
                    selectedInstalled = -1;
                    installedMaps = GetInstalledMaps();
                    statusMessage = "Deleted: " + m.name;
                }
            }
            else {
                ImGui::TextDisabled("Select a map on the left.");
            }
            ImGui::EndChild();
            ImGui::Columns(1);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (isDownloading) {
        ImGui::Separator();
        ImGui::Text("Downloading: %s", downloadingMapName.c_str());
        ImGui::ProgressBar(downloadProgress, ImVec2(-1, 0));
    }
    else if (!statusMessage.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("Status: %s", statusMessage.c_str());
    }

    ImGui::End();

    if (pendingLaunch) {
        pendingLaunch = false;
        std::string mapPath = pendingMapPath;
        gameWrapper->SetTimeout([this, mapPath](GameWrapper* gw) {
            cvarManager->executeCommand("load_workshop \"" + mapPath + "\"", true);
            }, 0.5f);
    }

    if (!windowOpen) {
        windowOpen = true;
        cvarManager->executeCommand("togglemenu custommaps");
    }
}