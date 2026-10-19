#pragma once

#include <array>
#include <chrono>

#include <BML/BMLAll.h>

MOD_EXPORT IMod *BMLEntry(IBML *bml);
MOD_EXPORT void BMLExit(IMod *mod);

class SpeedrunShortcuts final : public IMod {
public:
    explicit SpeedrunShortcuts(IBML *bml) : IMod(bml) {}

    const char *GetID() override { return "SpeedrunShortcuts"; }
    const char *GetVersion() override { return "0.2.3-native.1"; }
    const char *GetName() override { return "Speedrun Shortcuts"; }
    const char *GetAuthor() override { return "BallanceBug; native port by gzz2000"; }
    const char *GetDescription() override { return "Keyboard shortcuts useful for speedrunning."; }
    DECLARE_BML_VERSION;

    void OnLoad() override;
    void OnModifyConfig(const char *category, const char *key, IProperty *property) override;
    void OnLoadScript(const char *filename, CKBehavior *script) override;
    void OnProcess() override;

    void OnBallNavActive() override { m_BallNavInactive = false; }
    void OnBallNavInactive() override { m_BallNavInactive = true; }
    void OnPreLoadLevel() override;
    void OnPreExitLevel() override;
    void OnPreNextLevel() override;

private:
    enum KeySetting {
        CheatToggle,
        SetSpawn,
        Restart,
        SetSector,
        Modifier,
        KeySettingCount,
    };

    void RefreshSettings();
    bool EnsureGameplayReferences();
    bool ConfirmRestart();
    void SetSpawnPoint();
    void RestartLevel();
    void SetCurrentSector(int sector);
    void ResetBall(unsigned int generation);
    void InvalidateLevelState();

    InputHook *m_Input = nullptr;
    CKContext *m_Context = nullptr;
    std::array<IProperty *, KeySettingCount> m_KeyProperties{};
    std::array<CKKEYBOARD, KeySettingCount> m_Keys{};
    IProperty *m_StrictModeProperty = nullptr;
    bool m_StrictMode = false;
    bool m_BallNavInactive = true;

    CKDataArray *m_CurrentLevel = nullptr;
    CKDataArray *m_Checkpoints = nullptr;
    CKDataArray *m_ResetPoints = nullptr;
    CKDataArray *m_IngameParameter = nullptr;
    CKParameter *m_CurrentSector = nullptr;
    CKBehavior *m_DynamicPosition = nullptr;
    CKBehavior *m_PhysicalizeNewBall = nullptr;
    CK_ID m_KeyEventOutputId = 0;
    CK_ID m_RestartOutputId = 0;
    unsigned int m_LevelGeneration = 1;

    std::chrono::steady_clock::time_point m_LastRestartPress{};
};
