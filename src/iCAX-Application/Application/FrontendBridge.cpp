#include "pch.h"
#include "FrontendBridge.h"

#include "ApplicationHost/ApplicationHost.h"
#include "Data/uuid.h"
#include "Mailbox/MailPayload.h"
#include "Mailbox/MailPostOffice.h"

#include <map>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    std::string _ToString(IN const iCAX::Data::uuid& ID_)
    {
        return iCAX::Data::to_string(ID_);
    }

    iCAX::Data::uuid _ParseChannelID(IN const std::string& strChannelID_)
    {
        auto _ID = iCAX::Data::uuid::from_string(strChannelID_);
        if (!_ID || _ID->is_nil())
        {
            throw std::invalid_argument("Invalid channel id: " + strChannelID_);
        }
        return *_ID;
    }

    iCAX::Frontend::CFrontendMailEnvelope _ToEnvelope(
        IN const iCAX::Data::uuid& ChannelID_,
        IN const iCAX::Mail::Mail& Mail_)
    {
        iCAX::Frontend::CFrontendMailEnvelope _Envelope;
        _Envelope.ChannelID = _ToString(ChannelID_);
        _Envelope.nID = Mail_.Header.nMailId;
        _Envelope.nOriginID = Mail_.Header.nOriginId;
        _Envelope.nTypeCode = Mail_.Header.nTypeCode;
        _Envelope.nStamp = static_cast<uint16_t>(Mail_.Header.nStamp);
        _Envelope.PayloadText = iCAX::Mail::GetMailPayloadText(Mail_);
        return _Envelope;
    }

    iCAX::Mail::Mail _ToMail(IN const iCAX::Frontend::CFrontendMailEnvelope& Envelope_)
    {
        iCAX::Mail::MailHeader _Header;
        _Header.nMailId = Envelope_.nID;
        _Header.nOriginId = Envelope_.nOriginID;
        _Header.nTypeCode = Envelope_.nTypeCode;
        _Header.nStamp = static_cast<iCAX::Mail::StampCode>(Envelope_.nStamp);
        return iCAX::Mail::CreateTextMail(_Header, Envelope_.PayloadText);
    }
}

class iCAX::Application::CFrontendBridge::Impl final
{
public:
    using PostOfficeResolver = std::function<iCAX::Mail::CMailPostOffice()>;

    struct RegisteredChannel final
    {
        PostOfficeResolver Resolver;
        bool bApplicationChannel = false;
    };

    mutable std::mutex Mutex;
    iCAX::ApplicationHost::CApplicationHost* pEngine = nullptr;
    std::map<iCAX::Data::uuid, RegisteredChannel> FrontendChannels;
    FrontendMailHandler MailHandler;

public:
    iCAX::ApplicationHost::CApplicationHost& RequireEngine() const
    {
        std::lock_guard<std::mutex> _Lock(Mutex);
        if (!pEngine)
        {
            throw std::logic_error("FrontendBridge is not attached to ApplicationHost");
        }
        return *pEngine;
    }

    void RegisterChannel(
        IN const iCAX::Data::uuid& ChannelID_,
        IN PostOfficeResolver Resolver_,
        IN bool bApplicationChannel_)
    {
        if (ChannelID_.is_nil())
        {
            throw std::invalid_argument("Channel id cannot be nil");
        }
        if (!Resolver_)
        {
            throw std::invalid_argument("Frontend post office resolver is empty");
        }

        auto _PostOffice = Resolver_();
        if (!_PostOffice.IsValid())
        {
            throw std::invalid_argument("Frontend post office is not valid");
        }

        std::lock_guard<std::mutex> _Lock(Mutex);
        if (!pEngine)
        {
            throw std::logic_error("FrontendBridge is not attached to ApplicationHost");
        }
        FrontendChannels[ChannelID_] = RegisteredChannel{ std::move(Resolver_), bApplicationChannel_ };
    }

    iCAX::Mail::CMailPostOffice ResolveRegisteredPostOffice(IN const iCAX::Data::uuid& ChannelID_) const
    {
        RegisteredChannel _Channel;
        {
            std::lock_guard<std::mutex> _Lock(Mutex);
            auto _Iter = FrontendChannels.find(ChannelID_);
            if (_Iter == FrontendChannels.end())
            {
                throw std::runtime_error("Frontend post office is not registered: " + _ToString(ChannelID_));
            }
            _Channel = _Iter->second;
        }
        return _Channel.Resolver();
    }

    std::vector<std::pair<iCAX::Data::uuid, RegisteredChannel>> SnapshotChannels() const
    {
        std::lock_guard<std::mutex> _Lock(Mutex);
        std::vector<std::pair<iCAX::Data::uuid, RegisteredChannel>> _Channels;
        _Channels.reserve(FrontendChannels.size());
        for (const auto& _Item : FrontendChannels)
        {
            _Channels.emplace_back(_Item.first, _Item.second);
        }
        return _Channels;
    }

    void RemoveChannelIfCurrent(IN const iCAX::Data::uuid& ChannelID_, IN bool bAllowApplicationChannel_)
    {
        std::lock_guard<std::mutex> _Lock(Mutex);
        auto _Iter = FrontendChannels.find(ChannelID_);
        if (_Iter == FrontendChannels.end())
        {
            return;
        }
        if (_Iter->second.bApplicationChannel && !bAllowApplicationChannel_)
        {
            return;
        }
        FrontendChannels.erase(_Iter);
    }
};

iCAX::Application::CFrontendBridge::CFrontendBridge()
    : m_pImpl(std::make_unique<Impl>())
{
}

iCAX::Application::CFrontendBridge::~CFrontendBridge() = default;

void iCAX::Application::CFrontendBridge::Attach(iCAX::ApplicationHost::CApplicationHost& Engine_)
{
    if (!Engine_.IsRunning())
    {
        throw std::logic_error("FrontendBridge can only attach to a running ApplicationHost");
    }

    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->pEngine = &Engine_;
    m_pImpl->FrontendChannels.clear();

    const auto _ApplicationChannelID = Engine_.GetApplicationChannelID();
    if (_ApplicationChannelID.is_nil())
    {
        m_pImpl->pEngine = nullptr;
        throw std::logic_error("Application channel id is nil");
    }
    auto _ApplicationPostOffice = Engine_.GetApplicationFrontendPostOffice();
    if (!_ApplicationPostOffice.IsValid())
    {
        m_pImpl->pEngine = nullptr;
        throw std::logic_error("Application frontend post office is not valid");
    }
    m_pImpl->FrontendChannels[_ApplicationChannelID] = Impl::RegisteredChannel{
        [&Engine_]() {
            return Engine_.GetApplicationFrontendPostOffice();
        },
        true
    };
}

void iCAX::Application::CFrontendBridge::Detach()
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->MailHandler = nullptr;
    m_pImpl->FrontendChannels.clear();
    m_pImpl->pEngine = nullptr;
}

bool iCAX::Application::CFrontendBridge::IsAttached() const
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    return m_pImpl->pEngine != nullptr;
}

std::string iCAX::Application::CFrontendBridge::GetApplicationChannelIDText() const
{
    auto& _Engine = m_pImpl->RequireEngine();
    return _ToString(_Engine.GetApplicationChannelID());
}

std::string iCAX::Application::CFrontendBridge::RegisterProductChannel(const std::string& strProductID_)
{
    auto& _Engine = m_pImpl->RequireEngine();
    auto _pRuntime = _Engine.FindProductRuntime(strProductID_);
    if (!_pRuntime)
    {
        throw std::runtime_error("Product runtime is not started: " + strProductID_);
    }

    const auto _ChannelID = _pRuntime->GetProductChannelID();
    m_pImpl->RegisterChannel(
        _ChannelID,
        [&_Engine, strProductID_]() {
            return _Engine.GetProductFrontendPostOffice(strProductID_);
        },
        false);
    return _ToString(_ChannelID);
}

std::string iCAX::Application::CFrontendBridge::RegisterSceneChannel(
    const std::string& strProjectID_,
    const std::string& strSceneID_)
{
    auto& _Engine = m_pImpl->RequireEngine();
    auto _ProjectID = _ParseChannelID(strProjectID_);
    auto _SceneID = _ParseChannelID(strSceneID_);
    for (const auto& _pRuntime : _Engine.GetProductRuntimes())
    {
        if (!_pRuntime)
        {
            continue;
        }

        auto _pCatalog = _pRuntime->FindProjectCatalogByProjectID(_ProjectID);
        if (!_pCatalog)
        {
            continue;
        }

        auto _pProject = _pCatalog->FindProject(_ProjectID);
        if (!_pProject)
        {
            continue;
        }

        auto _pScene = _pProject->GetScene(_SceneID);
        if (!_pScene)
        {
            continue;
        }

        const auto _SceneChannelID = _pScene->GetSceneChannelID();
        m_pImpl->RegisterChannel(
            _SceneChannelID,
            [&_Engine, _ProjectID, _SceneID]() {
                return _Engine.GetSceneFrontendPostOffice(_ProjectID, _SceneID);
            },
            false);
        return _ToString(_SceneChannelID);
    }

    throw std::runtime_error("Scene runtime is not found: " + strProjectID_ + "/" + strSceneID_);
}

void iCAX::Application::CFrontendBridge::PostMail(const CFrontendMailEnvelope& Envelope_)
{
    const auto _ChannelID = _ParseChannelID(Envelope_.ChannelID);
    auto _PostOffice = m_pImpl->ResolveRegisteredPostOffice(_ChannelID);
    auto _Mail = _ToMail(Envelope_);

    try
    {
        _PostOffice.Send(_Mail);
    }
    catch (...)
    {
        iCAX::Mail::ReleaseMailPayload(_Mail);
        throw;
    }

    iCAX::Mail::ReleaseMailPayload(_Mail);
}

std::vector<iCAX::Application::CFrontendMailEnvelope> iCAX::Application::CFrontendBridge::PollMails()
{
    auto _Channels = m_pImpl->SnapshotChannels();

    std::vector<CFrontendMailEnvelope> _Result;
    for (const auto& _Item : _Channels)
    {
        iCAX::Mail::CMailPostOffice _PostOffice;
        try
        {
            _PostOffice = _Item.second.Resolver();
            if (!_PostOffice.IsValid())
            {
                m_pImpl->RemoveChannelIfCurrent(_Item.first, false);
                continue;
            }
        }
        catch (...)
        {
            m_pImpl->RemoveChannelIfCurrent(_Item.first, false);
            continue;
        }

        auto _Mails = _PostOffice.Receive();
        for (auto& _Mail : _Mails)
        {
            auto _Envelope = _ToEnvelope(_Item.first, _Mail);
            _Result.push_back(_Envelope);

            FrontendMailHandler _Handler;
            {
                std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
                _Handler = m_pImpl->MailHandler;
            }
            if (_Handler)
            {
                _Handler(_Envelope);
            }

            iCAX::Mail::ReleaseMailPayload(_Mail);
        }
    }
    return _Result;
}

void iCAX::Application::CFrontendBridge::SetMailHandler(FrontendMailHandler Handler_)
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->MailHandler = std::move(Handler_);
}
