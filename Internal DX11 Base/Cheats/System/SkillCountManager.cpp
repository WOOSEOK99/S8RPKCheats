#include "SkillCountManager.h"
#include "../../pch.h"
#include "../../showlog.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace DX11Base {

    std::unordered_map<int, std::unordered_map<uintptr_t, int>> g_customSkillCounts;
    std::mutex g_skillCountMutex;
    extern HMODULE g_hModule;

    void SaveSkillCounts() {
        std::lock_guard<std::mutex> lock(g_skillCountMutex);
        char path[MAX_PATH];
        if (!GetModuleFileNameA(g_hModule, path, MAX_PATH)) return;

        std::string jsonPath = std::filesystem::path(path).parent_path().append("S8RPK_skill_counts.json").string();
        std::ofstream file(jsonPath);
        if (!file.is_open()) return;

        file << "{\n";
        bool firstOfficer = true;
        for (auto const& [offID, skills] : g_customSkillCounts) {
            if (!firstOfficer) file << ",\n";
            file << "  \"" << offID << "\": {\n";
            bool firstSkill = true;
            for (auto const& [offset, count] : skills) {
                if (!firstSkill) file << ",\n";
                file << "    \"" << offset << "\": " << count;
                firstSkill = false;
            }
            file << "\n  }";
            firstOfficer = false;
        }
        file << "\n}\n";
        file.close();
    }

    void LoadSkillCounts() {
        std::lock_guard<std::mutex> lock(g_skillCountMutex);
        char path[MAX_PATH];
        if (!GetModuleFileNameA(g_hModule, path, MAX_PATH)) return;

        std::string jsonPath = std::filesystem::path(path).parent_path().append("S8RPK_skill_counts.json").string();
        if (!std::filesystem::exists(jsonPath)) return;

        std::ifstream file(jsonPath);
        if (!file.is_open()) return;

        g_customSkillCounts.clear();

        std::string line;
        int currentOffID = -1;
        while (std::getline(file, line)) {
            // Very basic parser for the specific format we save
            if (line.find("{") != std::string::npos && line.find(":") != std::string::npos) {
                // Potential start of officer block: "ID": {
                size_t start = line.find("\"");
                size_t end = line.find("\"", start + 1);
                if (start != std::string::npos && end != std::string::npos) {
                    try { currentOffID = std::stoi(line.substr(start + 1, end - start - 1)); }
                    catch (...) { currentOffID = -1; }
                }
            }
            else if (line.find(":") != std::string::npos && currentOffID != -1) {
                // Potential skill line: "Offset": Count
                size_t start = line.find("\"");
                size_t end = line.find("\"", start + 1);
                size_t colon = line.find(":", end);
                if (start != std::string::npos && end != std::string::npos && colon != std::string::npos) {
                    try {
                        uintptr_t offset = (uintptr_t)std::stoull(line.substr(start + 1, end - start - 1));
                        std::string valStr = line.substr(colon + 1);
                        size_t comma = valStr.find(",");
                        if (comma != std::string::npos) valStr = valStr.substr(0, comma);
                        int count = std::stoi(valStr);
                        g_customSkillCounts[currentOffID][offset] = count;
                    }
                    catch (...) {}
                }
            }
            if (line.find("}") != std::string::npos && line.find(":") == std::string::npos) {
                currentOffID = -1;
            }
        }
        file.close();
    }

    int GetTargetSkillCount(int officerID, uintptr_t offset) {
        std::lock_guard<std::mutex> lock(g_skillCountMutex);
        if (g_customSkillCounts.count(officerID) && g_customSkillCounts[officerID].count(offset)) {
            return g_customSkillCounts[officerID][offset];
        }
        return -1; // No custom count set
    }

    void SetTargetSkillCount(int officerID, uintptr_t offset, int count) {
        {
            std::lock_guard<std::mutex> lock(g_skillCountMutex);
            if (count > 0) {
                g_customSkillCounts[officerID][offset] = count;
            } else {
                if (g_customSkillCounts.count(officerID)) {
                    g_customSkillCounts[officerID].erase(offset);
                    if (g_customSkillCounts[officerID].empty()) {
                        g_customSkillCounts.erase(officerID);
                    }
                }
            }
        }
        SaveSkillCounts();
    }

}
