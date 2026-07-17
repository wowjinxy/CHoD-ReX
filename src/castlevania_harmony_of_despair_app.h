// castlevania_harmony_of_despair - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdint>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/flags.h>

class CastlevaniaHarmonyOfDespairApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<CastlevaniaHarmonyOfDespairApp>(new CastlevaniaHarmonyOfDespairApp(ctx, "castlevania_harmony_of_despair",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // void OnShutdown() override {}
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
      REXLOG_INFO("Using default GPU plugin: {}", config.gpu_plugin);
    }
  }

  bool SetupPresentation() override {
    if (!rex::ReXApp::SetupPresentation())
      return false;

    window()->SetTitle("Castlevania: Harmony of Despair");
    return true;
  }

  void OnPostInitLogging() override { ConfigureLicenseMask(); }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty())
      return;

    const auto exe_dir = rex::filesystem::GetExecutableFolder();
    for (const auto& candidate : {
             exe_dir / "assets",
             exe_dir / ".." / ".." / ".." / "assets",
             exe_dir / ".." / ".." / ".." / ".." / "Castlevania-Harmony-of-Despair",
             std::filesystem::current_path() / "assets",
             std::filesystem::current_path() / ".." / "Castlevania-Harmony-of-Despair",
         }) {
      std::error_code ec;
      auto resolved = std::filesystem::weakly_canonical(candidate, ec);
      if (!ec && std::filesystem::is_directory(resolved)) {
        paths.game_data_root = resolved;
        return;
      }
    }
  }

 private:
  static constexpr uint32_t kTitleId = 0x58410A7A;

  void ConfigureLicenseMask()
   {
    if (REXCVAR_GET(license_mask) != 0) {
      REXLOG_INFO("Using configured license_mask: 0x{:08X}", REXCVAR_GET(license_mask));
      return;
    }

    REXCVAR_SET(license_mask, 1);
  }
};
