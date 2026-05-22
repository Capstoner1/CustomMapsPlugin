#pragma once
#include "bakkesmod/plugin/bakkesmodplugin.h"
#include "bakkesmod/wrappers/ImageWrapper.h"
#include "GuiBase.h"
#include <string>
#include <vector>
#include <set>
#include <filesystem>

struct MapEntry {
    std::string name;
    std::string author;
    std::string description;
    std::string downloadUrl;
    std::string category;
    std::string filePath;
    std::string previewUrl;
    std::string fileSize;
};

class CustomMapsPlugin : public BakkesMod::Plugin::BakkesModPlugin, public PluginWindowBase {
public:
    void onLoad() override;
    void onUnload() override;
    void Render() override;

private:
    void EnsureFolderExists();
    void LoadMapIndex();
    void DownloadAndInstallMap(const MapEntry& map);
    void LaunchMap(const std::filesystem::path& mapPath);
    std::vector<MapEntry> GetInstalledMaps();
    std::filesystem::path GetModsFolder();
    std::filesystem::path GetImageCacheFolder();
    std::filesystem::path GetFavoritesPath();
    void LoadPreviewImage(const std::string& url);
    void LoadFavorites();
    void SaveFavorites();
    bool IsFavorite(const std::string& mapName);
    void ToggleFavorite(const std::string& mapName);

    std::vector<MapEntry> maps;
    std::vector<MapEntry> filteredMaps;
    std::vector<MapEntry> installedMaps;
    std::set<std::string> favorites;
    char searchBuf[256] = {};
    char authorSearchBuf[256] = {};
    char favSearchBuf[256] = {};
    char instSearchBuf[256] = {};
    std::string statusMessage;
    int selectedMap = -1;
    int lastSelectedMap = -2;
    int selectedFav = -1;
    int selectedInstalled = -1;
    bool windowOpen = true;
    bool pendingLaunch = false;
    bool showFavoritesOnly = false;
    std::string pendingMapPath = "";
    std::string currentPreviewUrl = "";
    bool loadingPreview = false;
    std::shared_ptr<ImageWrapper> previewImage = nullptr;

    float downloadProgress = 0.0f;
    bool isDownloading = false;
    std::string downloadingMapName = "";
};