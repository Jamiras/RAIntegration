#include "GameIdentifier.hh"

#include "RA_Log.h"
#include "RA_md5factory.h"
#include "RA_StringUtils.h"

#include <rc_hash.h>

#include "api\ResolveHash.hh"

#include "data\context\ConsoleContext.hh"
#include "data\context\EmulatorContext.hh"
#include "data\context\GameContext.hh"
#include "data\context\UserContext.hh"
#include "data\context\SessionTracker.hh"

#include "services\IAudioSystem.hh"
#include "services\IConfiguration.hh"
#include "services\ServiceLocator.hh"

#include "ui\viewmodels\MessageBoxViewModel.hh"
#include "ui\viewmodels\OverlayManager.hh"
#include "ui\viewmodels\UnknownGameViewModel.hh"
#include "ui\viewmodels\WindowManager.hh"

namespace ra {
namespace services {

unsigned int GameIdentifier::IdentifyGame(const BYTE* pROM, size_t nROMSize)
{
    m_nPendingMode = ra::data::context::GameContext::Mode::Normal;

    const auto nConsoleId = ra::services::ServiceLocator::Get<ra::data::context::ConsoleContext>().Id();
    if (nConsoleId == 0)
    {
        ra::ui::viewmodels::MessageBoxViewModel::ShowErrorMessage(L"Cannot identify game for unknown console.");
        return 0U;
    }

    if (pROM == nullptr || nROMSize == 0)
    {
        m_sPendingMD5.clear();
        m_nPendingGameId = 0U;
        return 0U;
    }

    char hash[33];
    rc_hash_generate_from_buffer(hash, nConsoleId, const_cast<BYTE*>(pROM), nROMSize);

    return IdentifyHash(hash);
}

unsigned int GameIdentifier::IdentifyHash(const std::string& sMD5)
{
    if (!ra::services::ServiceLocator::Get<ra::data::context::UserContext>().IsLoggedIn())
    {
        ra::ui::viewmodels::MessageBoxViewModel::ShowErrorMessage(L"Cannot load achievements",
            L"You must be logged in to load achievements. Please reload the game after logging in.");
        return 0U;
    }

    unsigned int nGameId = 0U;
    m_nPendingMode = ra::data::context::GameContext::Mode::Normal;

    ra::api::ResolveHash::Request request;
    request.Hash = sMD5;

    const auto response = request.Call();
    if (response.Succeeded())
    {
        nGameId = response.GameId;
        if (nGameId == 0) // Unknown
        {
            RA_LOG_INFO("Could not identify game with hash %s", sMD5);

            auto sEstimatedGameTitle = ra::services::ServiceLocator::Get<ra::data::context::EmulatorContext>().GetGameTitle();

            ra::ui::viewmodels::UnknownGameViewModel vmUnknownGame;
            vmUnknownGame.InitializeGameTitles();
            vmUnknownGame.SetSystemName(ra::services::ServiceLocator::Get<ra::data::context::ConsoleContext>().Name());
            vmUnknownGame.SetChecksum(ra::Widen(sMD5));
            vmUnknownGame.SetEstimatedGameName(ra::Widen(sEstimatedGameTitle));
            vmUnknownGame.SetNewGameName(vmUnknownGame.GetEstimatedGameName());

            if (vmUnknownGame.ShowModal() == ra::ui::DialogResult::OK)
            {
                nGameId = vmUnknownGame.GetSelectedGameId();

                if (vmUnknownGame.GetTestMode())
                    m_nPendingMode = ra::data::context::GameContext::Mode::CompatibilityTest;
            }
        }
        else
        {
            RA_LOG_INFO("Successfully looked up game with ID %u", nGameId);
        }
    }
    else
    {
        std::wstring sErrorMessage = ra::Widen(response.ErrorMessage);
        if (sErrorMessage.empty())
        {
            auto& pConfiguration = ra::services::ServiceLocator::Get<ra::services::IConfiguration>();
            sErrorMessage = ra::StringPrintf(L"Error from %s", pConfiguration.GetHostName());
        }

        ra::ui::viewmodels::MessageBoxViewModel::ShowErrorMessage(L"Could not identify game.", sErrorMessage);
    }

    // store the hash and game id - will be used by _RA_ActivateGame (if called)
    m_sPendingMD5 = sMD5;
    m_nPendingGameId = nGameId;

    auto& pGameContext = ra::services::ServiceLocator::GetMutable<ra::data::context::GameContext>();
    if (nGameId != 0 && nGameId == pGameContext.GameId())
    {
        // same as currently loaded rom. assume user is switching disks and _RA_ActivateGame won't be called.
        // update the hash now. if it does get called, this is redundant.
        pGameContext.SetGameHash(sMD5);
        pGameContext.SetMode(m_nPendingMode);
    }

    return nGameId;
}

void GameIdentifier::ActivateGame(unsigned int nGameId)
{
    if (nGameId != 0)
    {
        if (!ra::services::ServiceLocator::Get<ra::data::context::UserContext>().IsLoggedIn())
        {
            ra::ui::viewmodels::MessageBoxViewModel::ShowErrorMessage(L"Cannot load achievements",
                L"You must be logged in to load achievements. Please reload the game after logging in.");
            return;
        }

        RA_LOG_INFO("Loading game %u", nGameId);

        auto& pOverlayManager = ra::services::ServiceLocator::GetMutable<ra::ui::viewmodels::OverlayManager>();
        pOverlayManager.ClearPopups();

        auto& pGameContext = ra::services::ServiceLocator::GetMutable<ra::data::context::GameContext>();
        pGameContext.LoadGame(nGameId, m_nPendingMode);
        pGameContext.SetGameHash((nGameId == m_nPendingGameId) ? m_sPendingMD5 : "");

        ra::services::ServiceLocator::GetMutable<ra::data::context::SessionTracker>().BeginSession(nGameId);

        auto& pConfiguration = ra::services::ServiceLocator::Get<ra::services::IConfiguration>();
        if (!pConfiguration.IsFeatureEnabled(ra::services::Feature::Hardcore))
        {
            bool bShowHardcorePrompt = false;
            if (pConfiguration.IsFeatureEnabled(ra::services::Feature::NonHardcoreWarning))
                bShowHardcorePrompt = pGameContext.Assets().HasCoreAssets();

            if (bShowHardcorePrompt)
            {
                ra::ui::viewmodels::MessageBoxViewModel vmWarning;
                vmWarning.SetHeader(L"Enable Hardcore mode?");
                vmWarning.SetMessage(L"You are loading a game with achievements and do not currently have hardcore mode enabled.");
                vmWarning.SetIcon(ra::ui::viewmodels::MessageBoxViewModel::Icon::Warning);
                vmWarning.SetButtons(ra::ui::viewmodels::MessageBoxViewModel::Buttons::YesNo);

                if (vmWarning.ShowModal() == ra::ui::DialogResult::Yes)
                    ra::services::ServiceLocator::GetMutable<ra::data::context::EmulatorContext>().EnableHardcoreMode(false);
            }
            else
            {
                const bool bLeaderboardsEnabled = pConfiguration.IsFeatureEnabled(ra::services::Feature::Leaderboards);

                ra::services::ServiceLocator::Get<ra::services::IAudioSystem>().PlayAudioFile(L"Overlay\\info.wav");
                ra::services::ServiceLocator::GetMutable<ra::ui::viewmodels::OverlayManager>().QueueMessage(
                    L"Playing in Softcore Mode",
                    bLeaderboardsEnabled ? L"Leaderboard entries will not be submitted." : L"");
            }
        }
    }
    else
    {
        RA_LOG_INFO("Unloading current game");

        ra::services::ServiceLocator::GetMutable<ra::data::context::SessionTracker>().EndSession();

        auto& pGameContext = ra::services::ServiceLocator::GetMutable<ra::data::context::GameContext>();
        pGameContext.LoadGame(0U, m_nPendingMode);
        pGameContext.SetGameHash((m_nPendingGameId == 0) ? m_sPendingMD5 : "");
    }

    ra::services::ServiceLocator::GetMutable<ra::data::context::EmulatorContext>().ResetMemoryModified();
}

void GameIdentifier::IdentifyAndActivateGame(const BYTE* pROM, size_t nROMSize)
{
    const auto nGameId = IdentifyGame(pROM, nROMSize);
    ActivateGame(nGameId);

    if (nGameId == 0 && pROM && nROMSize)
    {
        // game did not resolve, but still want to display "Playing GAMENAME" in Rich Presence
        auto sEstimatedGameTitle = ra::Widen(ra::services::ServiceLocator::Get<ra::data::context::EmulatorContext>().GetGameTitle());
        if (sEstimatedGameTitle.empty())
            sEstimatedGameTitle = L"Unknown";

        auto& pGameContext = ra::services::ServiceLocator::GetMutable<ra::data::context::GameContext>();
        pGameContext.SetGameTitle(sEstimatedGameTitle);
    }
}

} // namespace services
} // namespace ra
