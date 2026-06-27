#include "pch.h"
#include "FrontendBridge.h"

#include "ApplicationHost/ApplicationHost.h"
#include "Data/uuid.h"
#include "Mailbox/MailPayload.h"
#include "Mailbox/MailPostOffice.h"

#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

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
    mutable std::mutex Mutex;
    iCAX::ApplicationHost::CApplicationHost* pEngine = nullptr;
    std::map<iCAX::Data::uuid, iCAX::Mail::CMailPostOffice> FrontendPostOffices;
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

    void RegisterPostOffice(
        IN const iCAX::Data::uuid& ChannelID_,
        IN const iCAX::Mail::CMailPostOffice& PostOffice_)
    {
        if (ChannelID_.is_nil())
        {
            throw std::invalid_argument("Channel id cannot be nil");
        }
        if (!PostOffice_.IsValid())
        {
            throw std::invalid_argument("Frontend post office is not valid");
        }

        std::lock_guard<std::mutex> _Lock(Mutex);
        if (!pEngine)
        {
            throw std::logic_error("FrontendBridge is not attached to ApplicationHost");
        }
        FrontendPostOffices[ChannelID_] = PostOffice_;
    }

    iCAX::Mail::CMailPostOffice GetRegisteredPostOffice(IN const iCAX::Data::uuid& ChannelID_) const
    {
        std::lock_guard<std::mutex> _Lock(Mutex);
        auto _Iter = FrontendPostOffices.find(ChannelID_);
        if (_Iter == FrontendPostOffices.end())
        {
            throw std::runtime_error("Frontend post office is not registered: " + _ToString(ChannelID_));
        }
        return _Iter->second;
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
    m_pImpl->FrontendPostOffices.clear();

    const auto _ApplicationChannelID = Engine_.GetApplicationChannelID();
    auto _ApplicationPostOffice = Engine_.GetApplicationFrontendPostOffice();
    if (_ApplicationChannelID.is_nil())
    {
        m_pImpl->pEngine = nullptr;
        throw std::logic_error("Application channel id is nil");
    }
    if (!_ApplicationPostOffice.IsValid())
    {
        m_pImpl->pEngine = nullptr;
        throw std::logic_error("Application frontend post office is not valid");
    }
    m_pImpl->FrontendPostOffices[_ApplicationChannelID] = _ApplicationPostOffice;
}

void iCAX::Application::CFrontendBridge::Detach()
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->MailHandler = nullptr;
    m_pImpl->FrontendPostOffices.clear();
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
    m_pImpl->RegisterPostOffice(_ChannelID, _Engine.GetProductFrontendPostOffice(strProductID_));
    return _ToString(_ChannelID);
}

std::string iCAX::Application::CFrontendBridge::RegisterProjectChannel(const std::string& strProjectID_)
{
    auto& _Engine = m_pImpl->RequireEngine();
    auto _ProjectID = _ParseChannelID(strProjectID_);
    auto _PostOffice = _Engine.GetProjectFrontendPostOffice(_ProjectID);

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

        const auto _ProjectChannelID = _pProject->GetProjectChannelID();
        m_pImpl->RegisterPostOffice(_ProjectChannelID, _PostOffice);
        return _ToString(_ProjectChannelID);
    }

    throw std::runtime_error("Project runtime is not found: " + strProjectID_);
}

void iCAX::Application::CFrontendBridge::PostMail(const CFrontendMailEnvelope& Envelope_)
{
    const auto _ChannelID = _ParseChannelID(Envelope_.ChannelID);
    auto _PostOffice = m_pImpl->GetRegisteredPostOffice(_ChannelID);
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
    std::vector<std::pair<iCAX::Data::uuid, iCAX::Mail::CMailPostOffice>> _PostOffices;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        _PostOffices.reserve(m_pImpl->FrontendPostOffices.size());
        for (const auto& _Item : m_pImpl->FrontendPostOffices)
        {
            _PostOffices.emplace_back(_Item.first, _Item.second);
        }
    }

    std::vector<CFrontendMailEnvelope> _Result;
    for (const auto& _Item : _PostOffices)
    {
        auto _Mails = _Item.second.Receive();
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
