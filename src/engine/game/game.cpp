/* OpenHoW
 * Copyright (C) 2017-2019 Mark Sowden <markelswo@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../engine.h"
#include "../frontend.h"
#include "../model.h"
#include "../script/script_config.h"
#include "../Map.h"

#include "actor_manager.h"
#include "mode_base.h"
#include "game.h"

using namespace openhow;

std::string MapManifest::Serialize() {
  std::stringstream output;
  output << "{";
  output << R"("name":")" << name << "\",";
  output << R"("author":")" << author << "\",";
  output << R"("description":")" << description << "\",";
  if (!modes.empty()) {
    output << R"("modes":[)";
    for (size_t i = 0; i < modes.size(); ++i) {
      output << "\"" << modes[i] << "\"";
      if (i != modes.size() - 1) {
        output << ",";
      }
    }
    output << "],";
  }
  output << R"("ambientColour":")" <<
         std::to_string(ambient_colour.r) << " " <<
         std::to_string(ambient_colour.g) << " " <<
         std::to_string(ambient_colour.b) << "\",";
  output << R"("skyColourTop":")" <<
         std::to_string(sky_colour_top.r) << " " <<
         std::to_string(sky_colour_top.g) << " " <<
         std::to_string(sky_colour_top.b) << "\",";
  output << R"("skyColourBottom":")" <<
         std::to_string(sky_colour_bottom.r) << " " <<
         std::to_string(sky_colour_bottom.g) << " " <<
         std::to_string(sky_colour_bottom.b) << "\",";
  output << R"("sunColour":")" <<
         std::to_string(sun_colour.r) << " " <<
         std::to_string(sun_colour.g) << " " <<
         std::to_string(sun_colour.b) << "\",";
  output << R"("sunYaw":")" << std::to_string(sun_yaw) << "\",";
  output << R"("sunPitch":")" << std::to_string(sun_pitch) << "\",";
  output << R"("temperature":")" << temperature << "\",";
  output << R"("weather":")" << weather << "\",";
  output << R"("time":")" << time << "\",";
  // Fog
  output << R"("fogColour":")" <<
         std::to_string(fog_colour.r) << " " <<
         std::to_string(fog_colour.g) << " " <<
         std::to_string(fog_colour.b) << "\",";
  output << R"("fogIntensity":")" << std::to_string(fog_intensity) << "\",";
  output << R"("fogDistance":")" << std::to_string(fog_distance) << "\"";
  output << "}\n";
  return output.str();
}

/////////////////////////////////////////////////////////////

GameManager::GameManager() {
  plRegisterConsoleCommand("createmap", CreateMapCommand, "");
  plRegisterConsoleCommand("map", MapCommand, "");
  plRegisterConsoleCommand("maps", MapsCommand, "");
}

GameManager::~GameManager() {
  map_manifests_.clear();
}

void GameManager::Tick() {
  FrontEnd_Tick();

  if (active_mode_ == nullptr) {
    return;
  }

  if (ambient_emit_delay_ < g_state.sim_ticks) {
    const AudioSample* sample = ambient_samples_[rand() % MAX_AMBIENT_SAMPLES];
    if (sample != nullptr) {
      PLVector3 position = {
          plGenerateRandomf(TERRAIN_PIXEL_WIDTH),
          active_map_->GetTerrain()->GetMaxHeight(),
          plGenerateRandomf(TERRAIN_PIXEL_WIDTH)
      };
      engine->GetAudioManager()->PlayLocalSound(sample, position, {0, 0, 0}, true, 0.5f);
    }

    ambient_emit_delay_ = g_state.sim_ticks + TICKS_PER_SECOND + rand() % (7 * TICKS_PER_SECOND);
  }

  active_mode_->Tick();
}

void GameManager::LoadMap(const std::string& name) {
  MapManifest* manifest = engine->GetGameManager()->GetMapManifest(name);
  if (manifest == nullptr) {
    LogWarn("Failed to get map descriptor, \"%s\"\n", name.c_str());
    return;
  }

  //FrontEnd_SetState(FE_MODE_LOADING);

  Map* map = new Map(manifest);
  if (active_map_ != nullptr) {
    ActorManager::GetInstance()->DestroyActors();
    ModelManager::GetInstance()->DestroyModels();
    delete active_map_;
  }

  active_map_ = map;

  std::string sample_ext = "d";
  if (manifest->time != "day") {
    sample_ext = "n";
  }

  for (unsigned int i = 1, idx = 0; i < 4; ++i) {
    if (i < 3) {
      ambient_samples_[idx++] = engine->GetAudioManager()->CacheSample(
          "audio/amb_" + std::to_string(i) + sample_ext + ".wav", false);
    }
    ambient_samples_[idx++] =
        engine->GetAudioManager()->CacheSample("audio/batt_s" + std::to_string(i) + ".wav", false);
    ambient_samples_[idx++] =
        engine->GetAudioManager()->CacheSample("audio/batt_l" + std::to_string(i) + ".wav", false);
  }

  ambient_emit_delay_ = g_state.sim_ticks + rand() % 100;

  active_mode_ = new BaseGameMode();
  // call StartRound; deals with spawning everything in and other mode specific logic
  active_mode_->StartRound();

  /* todo: we should actually pause here and wait for user input
   *       otherwise players won't have time to read the loading screen */
  FrontEnd_SetState(FE_MODE_GAME);
}

void GameManager::UnloadMap() {
  for (auto& ambient_sample : ambient_samples_) {
    delete ambient_sample;
  }

  delete active_mode_;
  delete active_map_;
}

void GameManager::RegisterMapManifest(const std::string& path) {
  LogInfo("Registering map \"%s\"...\n", path.c_str());

  MapManifest manifest;
  try {
    ScriptConfig config(path);
    manifest.name = config.GetStringProperty("name", manifest.name);
    manifest.author = config.GetStringProperty("author", manifest.author);
    manifest.description = config.GetStringProperty("description", manifest.description);
    manifest.tile_directory = config.GetStringProperty("tileDirectory", manifest.tile_directory);
    manifest.modes = config.GetArrayStrings("modes");
    manifest.ambient_colour = config.GetColourProperty("ambientColour", manifest.ambient_colour);
    manifest.sky_colour_top = config.GetColourProperty("skyColourTop", manifest.sky_colour_top);
    manifest.sky_colour_bottom = config.GetColourProperty("skyColourBottom", manifest.sky_colour_bottom);
    manifest.sun_colour = config.GetColourProperty("sunColour", manifest.sun_colour);
    manifest.sun_yaw = config.GetFloatProperty("sunYaw", manifest.sun_yaw);
    manifest.sun_pitch = config.GetFloatProperty("sunPitch", manifest.sun_pitch);
    manifest.temperature = config.GetStringProperty("temperature", manifest.temperature);
    manifest.weather = config.GetStringProperty("weather", manifest.weather);
    manifest.time = config.GetStringProperty("time", manifest.time);

    // Fog
    manifest.fog_colour = config.GetColourProperty("fogColour", manifest.fog_colour);
    manifest.fog_intensity = config.GetFloatProperty("fogIntensity", manifest.fog_intensity);
    manifest.fog_distance = config.GetFloatProperty("fogDistance", manifest.fog_distance);
  } catch (const std::exception& e) {
    LogWarn("Failed to read map config, \"%s\"!\n%s\n", path.c_str(), e.what());
  }

  char temp_buf[64];
  plStripExtension(temp_buf, sizeof(temp_buf), plGetFileName(path.c_str()));

  manifest.filepath = path;
  manifest.filename = temp_buf;

  map_manifests_.insert(std::make_pair(temp_buf, manifest));
}

static void RegisterManifestInterface(const char* path) {
  engine->GetGameManager()->RegisterMapManifest(path);
}

void GameManager::RegisterMapManifests() {
  map_manifests_.clear();

  std::string scan_path = std::string(u_get_base_path()) + "/campaigns/" + u_get_mod_path() + "/maps";
  plScanDirectory(scan_path.c_str(), "map", RegisterManifestInterface, false);
}

MapManifest* GameManager::GetMapManifest(const std::string& name) {
  auto manifest = map_manifests_.find(name);
  if (manifest != map_manifests_.end()) {
    return &manifest->second;
  }

  LogWarn("Failed to get manifest for \"%s\"!\n", name.c_str());
  return nullptr;
}

MapManifest* GameManager::CreateManifest(const std::string& name) {
  // ensure the map doesn't exist already
  if(engine->GetGameManager()->GetMapManifest(name) != nullptr) {
    LogWarn("Unable to create map, it already exists!\n");
    return nullptr;
  }

  std::string path = std::string(u_get_full_path()) + "/maps/" + name + ".map";
  std::ofstream output(path);
  if(!output.is_open()) {
    LogWarn("Failed to write to \"%s\", aborting!n\"\n", path.c_str());
    return nullptr;
  }

  MapManifest manifest;
  output << manifest.Serialize();
  output.close();

  LogInfo("Wrote \"%s\"!\n", path.c_str());

  engine->GetGameManager()->RegisterMapManifest(path);
  return engine->GetGameManager()->GetMapManifest(name);
}

void GameManager::CreateMapCommand(unsigned int argc, char** argv) {
  if(argc < 2) {
    LogWarn("Invalid number of arguments, ignoring!\n");
    return;
  }

  MapManifest* manifest = engine->GetGameManager()->CreateManifest(argv[1]);
  if(manifest == nullptr) {
    return;
  }

  engine->GetGameManager()->LoadMap(argv[1]);
}

void GameManager::MapCommand(unsigned int argc, char** argv) {
  if (argc < 2) {
    LogWarn("Invalid number of arguments, ignoring!\n");
    return;
  }

  std::string mode = "singleplayer";
  const MapManifest* desc = engine->GetGameManager()->GetMapManifest(argv[1]);
  if (desc != nullptr && !desc->modes.empty()) {
    mode = desc->modes[0];
  }

  engine->GetGameManager()->LoadMap(argv[1]);
}

void GameManager::MapsCommand(unsigned int argc, char** argv) {
  if (engine->GetGameManager()->map_manifests_.empty()) {
    LogWarn("No maps available!\n");
    return;
  }

  for (auto manifest : engine->GetGameManager()->map_manifests_) {
    MapManifest* desc = &manifest.second;
    std::string out =
        desc->name + "/" + manifest.first +
            " : " + desc->description +
            " :";
    // print out all the supported modes
    for (size_t i = 0; i < desc->modes.size(); ++i) {
      out += (i == 0 ? " " : ", ") + desc->modes[i];
    }
    LogInfo("%s\n", out.c_str());
  }

  LogInfo("%u maps\n", engine->GetGameManager()->map_manifests_.size());
}