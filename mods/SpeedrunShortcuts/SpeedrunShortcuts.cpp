#include "SpeedrunShortcuts.h"

#include <algorithm>
#include <string>

using namespace ScriptHelper;

namespace {
constexpr auto kRestartConfirmationWindow = std::chrono::seconds(3);

CK_ID ObjectId(CKObject *object) {
    return object ? object->GetID() : 0;
}
}

IMod *BMLEntry(IBML *bml) {
    return new SpeedrunShortcuts(bml);
}

void BMLExit(IMod *mod) {
    delete mod;
}

void SpeedrunShortcuts::OnLoad() {
    m_Context = m_BML->GetCKContext();
    m_Input = m_BML->GetInputManager();

    IConfig *config = GetConfig();
    config->SetCategoryComment("Keys", "Set the keys used to trigger actions. Must be used with the Modifier key.");

    m_KeyProperties[CheatToggle] = config->GetProperty("Keys", "CheatToggle");
    m_KeyProperties[CheatToggle]->SetDefaultKey(CKKEY_C);
    m_KeyProperties[CheatToggle]->SetComment("The key for toggling Cheat Mode.");

    m_KeyProperties[SetSpawn] = config->GetProperty("Keys", "SetSpawn");
    m_KeyProperties[SetSpawn]->SetDefaultKey(CKKEY_S);
    m_KeyProperties[SetSpawn]->SetComment("The key for setting spawn points (under Cheat Mode).");

    m_KeyProperties[Restart] = config->GetProperty("Keys", "Restart");
    m_KeyProperties[Restart]->SetDefaultKey(CKKEY_E);
    m_KeyProperties[Restart]->SetComment("The key for restarting the current level.");

    m_KeyProperties[SetSector] = config->GetProperty("Keys", "SetSector");
    m_KeyProperties[SetSector]->SetDefaultKey(CKKEY_W);
    m_KeyProperties[SetSector]->SetComment("The key for setting the current sector (with a number key).");

    m_KeyProperties[Modifier] = config->GetProperty("Keys", "ModifierKey");
    m_KeyProperties[Modifier]->SetDefaultKey(CKKEY_LMENU);
    m_KeyProperties[Modifier]->SetComment("The key to use together with keys above to trigger different functions.");

    config->SetCategoryComment("BMMO", "BMMO-related compatibility settings.");
    m_StrictModeProperty = config->GetProperty("BMMO", "StrictMode");
    m_StrictModeProperty->SetDefaultBoolean(false);
    m_StrictModeProperty->SetComment(
        "Reserved for BMMO cheat detection compatibility; inactive when no native BMMO client is loaded.");

    RefreshSettings();
}

void SpeedrunShortcuts::OnModifyConfig(const char *, const char *, IProperty *) {
    RefreshSettings();
}

void SpeedrunShortcuts::RefreshSettings() {
    for (std::size_t i = 0; i < m_KeyProperties.size(); ++i) {
        if (m_KeyProperties[i])
            m_Keys[i] = m_KeyProperties[i]->GetKey();
    }
    if (m_StrictModeProperty)
        m_StrictMode = m_StrictModeProperty->GetBoolean();
}

void SpeedrunShortcuts::OnLoadScript(const char *, CKBehavior *script) {
    if (!script || !script->GetName())
        return;

    const std::string name = script->GetName();
    if (name == "Gameplay_Events") {
        if (CKBehavior *keyEvent = FindFirstBB(script, "Key Event"))
            m_KeyEventOutputId = ObjectId(keyEvent->GetOutput(0));

        if (CKBehaviorIO *input = script->GetInput(0)) {
            if (CKBehavior *identity = FindNextBB(script, input)) {
                if (CKParameterOut *output = identity->GetOutputParameter(0))
                    m_CurrentSector = output->GetDestination(0);
            }
        }
    } else if (name == "Menu_Pause") {
        if (CKBehavior *restart = FindFirstBB(script, "Restart Level"))
            m_RestartOutputId = ObjectId(restart->GetOutput(0));
    } else if (name == "Gameplay_Ingame") {
        CKBehavior *ballManager = FindFirstBB(script, "BallManager");
        CKBehavior *newBall = ballManager ? FindFirstBB(ballManager, "New Ball") : nullptr;
        m_DynamicPosition = ballManager ? FindNextBB(script, ballManager, "TT Set Dynamic Position") : nullptr;
        m_PhysicalizeNewBall = newBall ? FindFirstBB(newBall, "physicalize new Ball") : nullptr;
    }
}

void SpeedrunShortcuts::OnProcess() {
    if (!m_Input || !m_Keys[Modifier] || !m_Input->IsKeyDown(m_Keys[Modifier]))
        return;

    if (m_Keys[CheatToggle] && m_Input->IsKeyPressed(m_Keys[CheatToggle])) {
        const bool enable = !m_BML->IsCheatEnabled();
        m_BML->EnableCheat(enable);
        m_BML->SendIngameMessage(enable ? "Cheat mode enabled" : "Cheat mode disabled");
        return;
    }

    if (m_Keys[SetSpawn] && m_Input->IsKeyPressed(m_Keys[SetSpawn])) {
        SetSpawnPoint();
        return;
    }

    if (m_Keys[Restart] && m_Input->IsKeyPressed(m_Keys[Restart])) {
        if (ConfirmRestart())
            RestartLevel();
        return;
    }

    if (!m_Keys[SetSector] || !m_Input->IsKeyDown(m_Keys[SetSector]))
        return;

    for (int sector = 1; sector <= 9; ++sector) {
        const auto mainKey = static_cast<CKKEYBOARD>(static_cast<int>(CKKEY_1) + sector - 1);
        static constexpr CKKEYBOARD kNumpadKeys[] = {
            CKKEY_NUMPAD1, CKKEY_NUMPAD2, CKKEY_NUMPAD3,
            CKKEY_NUMPAD4, CKKEY_NUMPAD5, CKKEY_NUMPAD6,
            CKKEY_NUMPAD7, CKKEY_NUMPAD8, CKKEY_NUMPAD9,
        };
        const auto numpadKey = kNumpadKeys[sector - 1];
        if (m_Input->IsKeyPressed(mainKey) || m_Input->IsKeyPressed(numpadKey)) {
            SetCurrentSector(sector);
            return;
        }
    }
}

bool SpeedrunShortcuts::EnsureGameplayReferences() {
    if (!m_CurrentLevel)
        m_CurrentLevel = m_BML->GetArrayByName("CurrentLevel");
    if (!m_Checkpoints)
        m_Checkpoints = m_BML->GetArrayByName("Checkpoints");
    if (!m_ResetPoints)
        m_ResetPoints = m_BML->GetArrayByName("ResetPoints");
    if (!m_IngameParameter)
        m_IngameParameter = m_BML->GetArrayByName("IngameParameter");
    return m_CurrentLevel && m_Checkpoints && m_ResetPoints && m_IngameParameter && m_CurrentSector;
}

void SpeedrunShortcuts::SetSpawnPoint() {
    if (!m_BML->IsCheatEnabled() || !m_BML->IsIngame() || !EnsureGameplayReferences())
        return;

    CK3dEntity *cameraReference = m_BML->Get3dEntityByName("Cam_OrientRef");
    if (!cameraReference)
        return;

    VxMatrix matrix = cameraReference->GetWorldMatrix();
    for (int i = 0; i < 3; ++i) {
        std::swap(matrix[0][i], matrix[2][i]);
        matrix[0][i] = -matrix[0][i];
    }
    m_CurrentLevel->SetElementValue(0, 3, &matrix);
    m_BML->SendIngameMessage(("Set Spawn Point to (" +
                              std::to_string(matrix[3][0]) + ", " +
                              std::to_string(matrix[3][1]) + ", " +
                              std::to_string(matrix[3][2]) + ")").c_str());
}

bool SpeedrunShortcuts::ConfirmRestart() {
    if (!m_BML->IsIngame())
        return false;
    if (m_Keys[Restart] == m_Keys[Modifier])
        return true;

    const auto now = std::chrono::steady_clock::now();
    if (m_LastRestartPress.time_since_epoch().count() != 0 &&
        now - m_LastRestartPress <= kRestartConfirmationWindow) {
        m_LastRestartPress = {};
        return true;
    }

    m_LastRestartPress = now;
    char keyName[64] = {};
    m_Input->GetKeyName(m_Keys[Restart], keyName);
    m_BML->SendIngameMessage((std::string("Note: press <") + keyName +
                              "> again in 3 seconds to restart.").c_str());
    return false;
}

void SpeedrunShortcuts::RestartLevel() {
    if (!m_BML->IsIngame() || !m_Context)
        return;

    // The Windows release only applies StrictMode while connected to BMMO.
    // A native BMMO client is not available, so preserve its offline behavior.
    (void)m_StrictMode;
    (void)m_BallNavInactive;

    if (auto *keyEventOutput = static_cast<CKBehaviorIO *>(m_Context->GetObject(m_KeyEventOutputId)))
        keyEventOutput->Activate();

    const unsigned int generation = m_LevelGeneration;
    m_BML->AddTimer(CKDWORD{3}, [this, generation]() {
        if (generation != m_LevelGeneration || !m_BML->IsIngame())
            return;

        CKMessageManager *messages = m_BML->GetMessageManager();
        const CKMessageType reset = messages->AddMessageType("Reset Level");
        if (CKGroup *level = m_BML->GetGroupByName("Level"))
            messages->SendMessageSingle(reset, level);
        if (CKGroup *balls = m_BML->GetGroupByName("All_Balls"))
            messages->SendMessageSingle(reset, balls);

        if (auto *restartOutput = static_cast<CKBehaviorIO *>(m_Context->GetObject(m_RestartOutputId)))
            restartOutput->Activate();
    });
}

void SpeedrunShortcuts::SetCurrentSector(int sector) {
    if (!m_BML->IsCheatEnabled() || !m_BML->IsPlaying() || !EnsureGameplayReferences())
        return;

    const int checkpointCount = m_Checkpoints->GetRowCount();
    if (sector < 1 || sector > checkpointCount + 1)
        return;

    int currentSector = GetParamValue<int>(m_CurrentSector);
    if (currentSector == sector)
        return;

    VxMatrix matrix;
    m_ResetPoints->GetElementValue(sector - 1, 0, &matrix);
    m_CurrentLevel->SetElementValue(0, 3, &matrix);
    m_IngameParameter->SetElementValue(0, 1, &sector);
    m_IngameParameter->SetElementValue(0, 2, &currentSector);
    SetParamValue(m_CurrentSector, sector);
    m_BML->SendIngameMessage(("Changed to Sector " + std::to_string(sector)).c_str());

    CKBehavior *sectorManager = m_BML->GetScriptByName("Gameplay_SectorManager");
    if (!sectorManager)
        return;
    m_Context->GetCurrentScene()->Activate(sectorManager, true);

    const unsigned int generation = m_LevelGeneration;
    m_BML->AddTimerLoop(CKDWORD{1}, [this, generation, sector, sectorManager]() {
        if (generation != m_LevelGeneration)
            return false;
        if (sectorManager->IsActive())
            return true;

        m_BML->AddTimer(CKDWORD{2}, [this, generation, sector]() {
            if (generation != m_LevelGeneration || !EnsureGameplayReferences())
                return;

            CKBOOL active = false;
            m_CurrentLevel->SetElementValue(0, 4, &active);

            CK_ID flameId = 0;
            m_Checkpoints->GetElementValue(sector % 2, 1, &flameId);
            auto *flame = static_cast<CK3dEntity *>(m_Context->GetObject(flameId));
            if (flame && flame->GetScriptCount() > 0)
                m_Context->GetCurrentScene()->Activate(flame->GetScript(0), true);

            m_Checkpoints->GetElementValue(sector - 1, 1, &flameId);
            flame = static_cast<CK3dEntity *>(m_Context->GetObject(flameId));
            if (flame && flame->GetScriptCount() > 0)
                m_Context->GetCurrentScene()->Activate(flame->GetScript(0), true);

            if (sector > m_Checkpoints->GetRowCount()) {
                CKMessageManager *messages = m_BML->GetMessageManager();
                const CKMessageType lastCheckpoint = messages->AddMessageType("last Checkpoint reached");
                if (CKGroup *sound = m_BML->GetGroupByName("All_Sound"))
                    messages->SendMessageSingle(lastCheckpoint, sound);
                ResetBall(generation);
                return;
            }

            m_BML->AddTimer(CKDWORD{2}, [this, generation, sector, flame]() {
                if (generation != m_LevelGeneration || !flame)
                    return;
                VxMatrix checkpointMatrix;
                m_Checkpoints->GetElementValue(sector - 1, 0, &checkpointMatrix);
                flame->SetWorldMatrix(checkpointMatrix);
                CKBOOL active = true;
                m_CurrentLevel->SetElementValue(0, 4, &active);
                if (flame->GetScriptCount() > 0)
                    m_Context->GetCurrentScene()->Activate(flame->GetScript(0), true);
                m_BML->Show(flame, CKSHOW, true);
                ResetBall(generation);
            });
        });
        return false;
    });
}

void SpeedrunShortcuts::ResetBall(unsigned int generation) {
    if (generation != m_LevelGeneration || !EnsureGameplayReferences() ||
        !m_DynamicPosition || !m_PhysicalizeNewBall)
        return;

    CKMessageManager *messages = m_BML->GetMessageManager();
    const CKMessageType deactivate = messages->AddMessageType("BallNav deactivate");
    if (CKGroup *gameplay = m_BML->GetGroupByName("All_Gameplay"))
        messages->SendMessageSingle(deactivate, gameplay);
    if (CKGroup *sound = m_BML->GetGroupByName("All_Sound"))
        messages->SendMessageSingle(deactivate, sound);

    m_BML->AddTimer(CKDWORD{2}, [this, generation]() {
        if (generation != m_LevelGeneration)
            return;
        auto *ball = static_cast<CK3dEntity *>(m_CurrentLevel->GetElementObject(0, 1));
        if (!ball)
            return;
        ExecuteBB::Unphysicalize(ball);
        m_DynamicPosition->ActivateInput(1);
        m_DynamicPosition->Activate();

        m_BML->AddTimer(CKDWORD{1}, [this, generation, ball]() {
            if (generation != m_LevelGeneration)
                return;
            VxMatrix matrix;
            m_CurrentLevel->GetElementValue(0, 3, &matrix);
            ball->SetWorldMatrix(matrix);
            if (CK3dEntity *camera = m_BML->Get3dEntityByName("Cam_MF")) {
                m_BML->RestoreIC(camera, true);
                camera->SetWorldMatrix(matrix);
            }

            m_BML->AddTimer(CKDWORD{1}, [this, generation]() {
                if (generation != m_LevelGeneration)
                    return;
                m_DynamicPosition->ActivateInput(0);
                m_DynamicPosition->Activate();
                m_PhysicalizeNewBall->ActivateInput(0);
                m_PhysicalizeNewBall->Activate();
                m_PhysicalizeNewBall->GetParent()->Activate();
            });
        });
    });
}

void SpeedrunShortcuts::InvalidateLevelState() {
    ++m_LevelGeneration;
    m_LastRestartPress = {};
}

void SpeedrunShortcuts::OnPreLoadLevel() {
    InvalidateLevelState();
}

void SpeedrunShortcuts::OnPreExitLevel() {
    InvalidateLevelState();
}

void SpeedrunShortcuts::OnPreNextLevel() {
    InvalidateLevelState();
}
