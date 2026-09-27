#include "SkillCountManager.h"
#include "../../pch.h"
#include "../../showlog.h"
#include "../../PerformanceDiagnostics.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace DX11Base {

    std::unordered_map<int, std::unordered_map<uintptr_t, int>> g_customSkillCounts;
    std::mutex g_skillCountMutex;
    extern HMODULE g_hModule;

    namespace {
        constexpr ULONGLONG kSkillCountSaveDebounceMs = 300;

        std::mutex s_skillCountSaveMutex;
        std::atomic<uint64_t> s_skillCountChangeVersion{0};
        std::atomic<uint64_t> s_skillCountSavedVersion{0};
        std::atomic<ULONGLONG> s_lastSkillCountChangeTick{0};

        bool WriteSkillCountsSnapshot(bool dirtyOnly) {
            const uint64_t changedBefore = s_skillCountChangeVersion.load(std::memory_order_acquire);
            const uint64_t savedBefore = s_skillCountSavedVersion.load(std::memory_order_acquire);
            if (dirtyOnly && changedBefore == savedBefore)
                return true;

            std::unordered_map<int, std::unordered_map<uintptr_t, int>> snapshot;
            uint64_t snapshotVersion = 0;
            {
                std::lock_guard<std::mutex> lock(g_skillCountMutex);
                snapshot = g_customSkillCounts;
                snapshotVersion = s_skillCountChangeVersion.load(std::memory_order_relaxed);
            }

            char path[MAX_PATH];
            if (!GetModuleFileNameA(g_hModule, path, MAX_PATH))
                return false;

            std::string jsonPath = std::filesystem::path(path).parent_path().append("S8RPK_skill_counts.json").string();

            PerfScope perfScope(PerfMetric::SkillCountSave);
            std::ofstream file(jsonPath, std::ios::trunc);
            if (!file.is_open())
                return false;

            file << "{\n";
            bool firstOfficer = true;
            for (auto const& [offID, skills] : snapshot) {
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
            file.flush();
            const bool writeOk = file.good();
            file.close();

            if (!writeOk)
                return false;

            // 저장 도중 새 변경이 생겼더라도 snapshotVersion까지만 저장된 것으로 표시합니다.
            // changeVersion이 더 앞서 있으면 다음 debounce에서 남은 변경을 다시 저장합니다.
            s_skillCountSavedVersion.store(snapshotVersion, std::memory_order_release);
            return true;
        }
    }

    void SaveSkillCounts() {
        std::lock_guard<std::mutex> saveLock(s_skillCountSaveMutex);
        WriteSkillCountsSnapshot(false);
    }

    void FlushPendingSkillCounts(bool force) {
        const uint64_t changed = s_skillCountChangeVersion.load(std::memory_order_acquire);
        const uint64_t saved = s_skillCountSavedVersion.load(std::memory_order_acquire);
        if (changed == saved)
            return;

        if (!force) {
            const ULONGLONG lastChange = s_lastSkillCountChangeTick.load(std::memory_order_acquire);
            const ULONGLONG now = GetTickCount64();
            if (lastChange != 0 && now >= lastChange && (now - lastChange) < kSkillCountSaveDebounceMs)
                return;
        }

        // 다른 조회 스레드가 이미 저장 중이면 기다리지 않습니다.
        // 완료 후에도 새 변경이 남아 있으면 다음 조회에서 다시 flush됩니다.
        std::unique_lock<std::mutex> saveLock(s_skillCountSaveMutex, std::try_to_lock);
        if (!saveLock.owns_lock())
            return;

        WriteSkillCountsSnapshot(true);
    }

    void LoadSkillCounts() {
        std::lock_guard<std::mutex> saveLock(s_skillCountSaveMutex);
        std::lock_guard<std::mutex> lock(g_skillCountMutex);
        char path[MAX_PATH];
        if (!GetModuleFileNameA(g_hModule, path, MAX_PATH)) return;

        std::string jsonPath = std::filesystem::path(path).parent_path().append("S8RPK_skill_counts.json").string();
        if (!std::filesystem::exists(jsonPath)) {
            s_skillCountChangeVersion.store(0, std::memory_order_relaxed);
            s_skillCountSavedVersion.store(0, std::memory_order_relaxed);
            s_lastSkillCountChangeTick.store(0, std::memory_order_relaxed);
            return;
        }

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

        // 로드된 상태는 이미 디스크와 동일하므로 dirty가 아닙니다.
        s_skillCountChangeVersion.store(0, std::memory_order_release);
        s_skillCountSavedVersion.store(0, std::memory_order_release);
        s_lastSkillCountChangeTick.store(0, std::memory_order_release);
    }

    int GetTargetSkillCount(int officerID, uintptr_t offset) {
        // UI/게임 로직의 자연스러운 조회 경로에서 debounce된 저장을 마무리합니다.
        // dirty가 없으면 원자 변수 비교만 하고 즉시 반환합니다.
        FlushPendingSkillCounts(false);

        std::lock_guard<std::mutex> lock(g_skillCountMutex);
        auto officerIt = g_customSkillCounts.find(officerID);
        if (officerIt != g_customSkillCounts.end()) {
            auto skillIt = officerIt->second.find(offset);
            if (skillIt != officerIt->second.end())
                return skillIt->second;
        }
        return -1; // No custom count set
    }

    void SetTargetSkillCount(int officerID, uintptr_t offset, int count) {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(g_skillCountMutex);

            auto officerIt = g_customSkillCounts.find(officerID);
            if (count > 0) {
                if (officerIt == g_customSkillCounts.end()) {
                    g_customSkillCounts[officerID][offset] = count;
                    changed = true;
                } else {
                    auto skillIt = officerIt->second.find(offset);
                    if (skillIt == officerIt->second.end() || skillIt->second != count) {
                        officerIt->second[offset] = count;
                        changed = true;
                    }
                }
            } else if (officerIt != g_customSkillCounts.end()) {
                const size_t erased = officerIt->second.erase(offset);
                if (erased != 0) {
                    changed = true;
                    if (officerIt->second.empty())
                        g_customSkillCounts.erase(officerIt);
                }
            }

            if (changed) {
                // map 변경과 version 증가를 같은 mutex 구간에서 처리해 snapshot과 version을 일치시킵니다.
                s_lastSkillCountChangeTick.store(GetTickCount64(), std::memory_order_relaxed);
                s_skillCountChangeVersion.fetch_add(1, std::memory_order_release);
            }
        }

        // 기존처럼 매 Set마다 전체 JSON을 쓰지 않습니다.
        // GetTargetSkillCount()의 반복 조회가 300ms debounce 이후 한 번만 flush합니다.
    }

}
