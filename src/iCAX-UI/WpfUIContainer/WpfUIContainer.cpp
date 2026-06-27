#include "framework.h"
#include "WpfUIContainer.h"

#include <msclr/gcroot.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#using <System.dll>
#using <WindowsBase.dll>
#using <PresentationCore.dll>
#using <PresentationFramework.dll>
#using <System.Xaml.dll>

using namespace System;
using namespace System::Collections::Generic;
using namespace System::Text;
using namespace System::Threading;
using namespace System::Windows;
using namespace System::Windows::Controls;
using namespace System::Windows::Input;
using namespace System::Windows::Media;
using namespace System::Windows::Threading;

namespace
{
    constexpr uint64_t kStartupHandshakeRequestID = 0x4955435857504655ull;

    String^ _ToManagedUTF8(IN const std::string& Text_)
    {
        if (Text_.empty())
        {
            return String::Empty;
        }

        auto _Bytes = gcnew array<Byte>(static_cast<int>(Text_.size()));
        pin_ptr<Byte> _Pinned = &_Bytes[0];
        std::memcpy(_Pinned, Text_.data(), Text_.size());
        return Encoding::UTF8->GetString(_Bytes);
    }

    std::string _ToNativeUTF8(IN String^ Text_)
    {
        if (String::IsNullOrEmpty(Text_))
        {
            return {};
        }

        auto _Bytes = Encoding::UTF8->GetBytes(Text_);
        if (_Bytes->Length == 0)
        {
            return {};
        }

        pin_ptr<Byte> _Pinned = &_Bytes[0];
        return std::string(reinterpret_cast<const char*>(_Pinned), static_cast<size_t>(_Bytes->Length));
    }

    uint32_t _CommandHash32(IN String^ Text_)
    {
        if (String::IsNullOrEmpty(Text_))
        {
            return 2166136261u;
        }

        uint32_t _Hash = 2166136261u;
        auto _Bytes = Encoding::UTF8->GetBytes(Text_);
        for each (Byte _Byte in _Bytes)
        {
            _Hash ^= static_cast<uint8_t>(_Byte);
            _Hash *= 16777619u;
        }
        return _Hash;
    }

    uint64_t _MakeCommandCode(IN String^ MainName_, IN String^ SubName_)
    {
        return (static_cast<uint64_t>(_CommandHash32(MainName_)) << 32)
            | static_cast<uint64_t>(_CommandHash32(SubName_));
    }

    String^ _EmptyObjectPayload()
    {
        return "{\"__variant_type\":\"Object\",\"value\":{}}";
    }

    String^ _JsonEscape(IN String^ Text_)
    {
        if (Text_ == nullptr)
        {
            return String::Empty;
        }

        auto _Builder = gcnew StringBuilder();
        for each (wchar_t _Ch in Text_)
        {
            switch (_Ch)
            {
            case L'\\':
                _Builder->Append("\\\\");
                break;
            case L'"':
                _Builder->Append("\\\"");
                break;
            case L'\b':
                _Builder->Append("\\b");
                break;
            case L'\f':
                _Builder->Append("\\f");
                break;
            case L'\n':
                _Builder->Append("\\n");
                break;
            case L'\r':
                _Builder->Append("\\r");
                break;
            case L'\t':
                _Builder->Append("\\t");
                break;
            default:
                if (_Ch < 0x20)
                {
                    _Builder->Append("\\u");
                    _Builder->Append(static_cast<int>(_Ch).ToString("x4"));
                }
                else
                {
                    _Builder->Append(_Ch);
                }
                break;
            }
        }
        return _Builder->ToString();
    }

    String^ _StringObjectPayload(IN String^ Key_, IN String^ Value_)
    {
        return String::Format(
            "{{\"__variant_type\":\"Object\",\"value\":{{\"{0}\":{{\"__variant_type\":\"string\",\"value\":\"{1}\"}}}}}}",
            _JsonEscape(Key_),
            _JsonEscape(Value_));
    }

    String^ _FormatStamp(IN uint16_t nStamp_)
    {
        return nStamp_ == 0
            ? "OK"
            : String::Format("Stamp {0}", nStamp_);
    }
}

namespace iCAX
{
    namespace Frontend
    {
        namespace Wpf
        {
            ref class CWpfRuntime;

            ref class CManagedMailEnvelope sealed
            {
            public:
                String^ ChannelID;
                UInt64 ID = 0;
                UInt64 OriginID = 0;
                UInt64 TypeCode = 0;
                UInt16 Stamp = 0;
                String^ PayloadText;
            };

            ref class CWpfMainWindow sealed : Window
            {
            public:
                explicit CWpfMainWindow(CWpfRuntime^ Runtime_);

            public:
                void SetApplicationChannelID(String^ ChannelID_);
                void SetStatus(String^ Text_);
                void AppendMail(CManagedMailEnvelope^ Mail_, String^ CommandName_);
                void SetLastPayload(String^ PayloadText_);

            private:
                Button^ MakeToolbarButton(String^ Text_, RoutedEventHandler^ Handler_);
                Border^ MakePanel(String^ Header_, UIElement^ Content_);
                void OnRefreshApplication(Object^ Sender_, RoutedEventArgs^ Args_);
                void OnListProducts(Object^ Sender_, RoutedEventArgs^ Args_);
                void OnStartProduct(Object^ Sender_, RoutedEventArgs^ Args_);
                void OnOpenProjectFile(Object^ Sender_, RoutedEventArgs^ Args_);
                void OnExit(Object^ Sender_, RoutedEventArgs^ Args_);

            private:
                CWpfRuntime^ m_Runtime;
                TextBlock^ m_StatusText;
                TextBlock^ m_ChannelText;
                ListBox^ m_MailList;
                TextBox^ m_PayloadText;
            };

            public ref class CWpfRuntime sealed
            {
            public:
                CWpfRuntime()
                    : m_Started(gcnew ManualResetEventSlim(false))
                    , m_Closed(gcnew ManualResetEventSlim(true))
                    , m_PendingCommands(gcnew Dictionary<UInt64, String^>())
                {
                }

            public:
                void Start(
                    IN iCAX::Frontend::IFrontendBridge* pBridge_,
                    IN uint32_t nStartupHandshakeTimeoutMS_)
                {
                    if (pBridge_ == nullptr)
                    {
                        throw gcnew InvalidOperationException("WPF UI container requires a FrontendBridge.");
                    }
                    if (!pBridge_->IsAttached())
                    {
                        throw gcnew InvalidOperationException("WPF UI container requires an attached FrontendBridge.");
                    }
                    if (m_Running)
                    {
                        return;
                    }

                    m_pBridge = pBridge_;
                    m_StartupException = nullptr;
                    m_Started->Reset();
                    m_Closed->Reset();

                    m_UIThread = gcnew Thread(gcnew ThreadStart(this, &CWpfRuntime::RunUIThread));
                    m_UIThread->Name = "iCAX WPF UI";
                    m_UIThread->SetApartmentState(ApartmentState::STA);
                    m_UIThread->IsBackground = false;
                    m_UIThread->Start();

                    const int _Timeout = nStartupHandshakeTimeoutMS_ == 0
                        ? 5000
                        : static_cast<int>(nStartupHandshakeTimeoutMS_);
                    if (!m_Started->Wait(_Timeout))
                    {
                        throw gcnew TimeoutException("WPF UI startup timed out.");
                    }
                    if (m_StartupException != nullptr)
                    {
                        throw m_StartupException;
                    }
                }

                void Stop()
                {
                    if (!m_Running)
                    {
                        return;
                    }

                    auto _Dispatcher = m_Dispatcher;
                    if (_Dispatcher != nullptr && !_Dispatcher->HasShutdownStarted)
                    {
                        _Dispatcher->BeginInvoke(gcnew Action(this, &CWpfRuntime::ShutdownOnUIThread));
                    }
                }

                void WaitForExit()
                {
                    m_Closed->Wait();
                    if (m_UIThread != nullptr && m_UIThread->IsAlive)
                    {
                        m_UIThread->Join();
                    }
                }

                bool IsRunning()
                {
                    return m_Running;
                }

                UInt64 SendApplicationCommand(IN String^ SubCommandName_, IN String^ PayloadText_)
                {
                    return SendCommand(m_ApplicationChannelID, "App", SubCommandName_, PayloadText_);
                }

                void OpenProjectFile()
                {
                    auto _Dialog = gcnew Microsoft::Win32::OpenFileDialog();
                    _Dialog->Title = "Open Project";
                    _Dialog->Filter = "iCAX project (*.icax)|*.icax|All files (*.*)|*.*";
                    _Dialog->CheckFileExists = true;
                    auto _Result = _Dialog->ShowDialog();
                    if (_Result.HasValue && _Result.Value)
                    {
                        SendApplicationCommand("OpenProjectFile", _StringObjectPayload("projectPath", _Dialog->FileName));
                    }
                }

            private:
                UInt64 SendCommand(
                    IN String^ ChannelID_,
                    IN String^ MainCommandName_,
                    IN String^ SubCommandName_,
                    IN String^ PayloadText_)
                {
                    if (String::IsNullOrWhiteSpace(ChannelID_))
                    {
                        throw gcnew InvalidOperationException("Command channel id is empty.");
                    }
                    if (m_pBridge == nullptr)
                    {
                        throw gcnew InvalidOperationException("FrontendBridge is not attached.");
                    }

                    try
                    {
                        iCAX::Frontend::CFrontendMailEnvelope _Envelope;
                        _Envelope.ChannelID = _ToNativeUTF8(ChannelID_);
                        _Envelope.nID = static_cast<uint64_t>(Interlocked::Increment(m_NextMailID));
                        _Envelope.nOriginID = 0;
                        _Envelope.nTypeCode = _MakeCommandCode(MainCommandName_, SubCommandName_);
                        _Envelope.nStamp = 0;
                        _Envelope.PayloadText = _ToNativeUTF8(String::IsNullOrEmpty(PayloadText_) ? _EmptyObjectPayload() : PayloadText_);

                        m_pBridge->PostMail(_Envelope);
                        m_PendingCommands[_Envelope.nID] = String::Format("{0}.{1}", MainCommandName_, SubCommandName_);
                        return _Envelope.nID;
                    }
                    catch (const std::exception& _Error)
                    {
                        throw gcnew InvalidOperationException(_ToManagedUTF8(_Error.what()));
                    }
                }

                void RunUIThread()
                {
                    try
                    {
                        m_ApplicationChannelID = _ToManagedUTF8(m_pBridge->GetApplicationChannelIDText());
                        if (String::IsNullOrWhiteSpace(m_ApplicationChannelID))
                        {
                            throw gcnew InvalidOperationException("Application channel id is empty.");
                        }

                        if (Application::Current != nullptr)
                        {
                            throw gcnew InvalidOperationException("A WPF Application already exists in this AppDomain.");
                        }

                        m_Application = gcnew Application();
                        m_Application->ShutdownMode = ShutdownMode::OnMainWindowClose;
                        m_Window = gcnew CWpfMainWindow(this);
                        m_Window->SetApplicationChannelID(m_ApplicationChannelID);

                        m_Dispatcher = Dispatcher::CurrentDispatcher;
                        m_MailTimer = gcnew DispatcherTimer();
                        m_MailTimer->Interval = TimeSpan::FromMilliseconds(16);
                        m_MailTimer->Tick += gcnew EventHandler(this, &CWpfRuntime::OnMailTimerTick);
                        m_MailTimer->Start();

                        m_Running = true;
                        m_Started->Set();

                        SendApplicationCommand("GetState", _EmptyObjectPayload());
                        SendApplicationCommand("ListProducts", _EmptyObjectPayload());

                        m_Application->Run(m_Window);
                    }
                    catch (Exception^ _Error)
                    {
                        m_StartupException = _Error;
                        m_Started->Set();
                    }
                    finally
                    {
                        m_Running = false;
                        m_MailTimer = nullptr;
                        m_Window = nullptr;
                        m_Application = nullptr;
                        m_Dispatcher = nullptr;
                        m_Closed->Set();
                    }
                }

                void ShutdownOnUIThread()
                {
                    if (m_MailTimer != nullptr)
                    {
                        m_MailTimer->Stop();
                    }
                    if (m_Application != nullptr)
                    {
                        m_Application->Shutdown();
                    }
                }

                void OnMailTimerTick(Object^, EventArgs^)
                {
                    if (m_pBridge == nullptr || m_Window == nullptr)
                    {
                        return;
                    }

                    try
                    {
                        auto _Mails = m_pBridge->PollMails();
                        for (const auto& _Mail : _Mails)
                        {
                            auto _ManagedMail = gcnew CManagedMailEnvelope();
                            _ManagedMail->ChannelID = _ToManagedUTF8(_Mail.ChannelID);
                            _ManagedMail->ID = _Mail.nID;
                            _ManagedMail->OriginID = _Mail.nOriginID;
                            _ManagedMail->TypeCode = _Mail.nTypeCode;
                            _ManagedMail->Stamp = _Mail.nStamp;
                            _ManagedMail->PayloadText = _ToManagedUTF8(_Mail.PayloadText);

                            String^ _CommandName = "Event";
                            if (_ManagedMail->OriginID != 0 && m_PendingCommands->ContainsKey(_ManagedMail->OriginID))
                            {
                                _CommandName = m_PendingCommands[_ManagedMail->OriginID];
                                m_PendingCommands->Remove(_ManagedMail->OriginID);
                            }

                            m_Window->AppendMail(_ManagedMail, _CommandName);
                            if (_ManagedMail->OriginID == kStartupHandshakeRequestID || _CommandName == "App.GetState")
                            {
                                m_Window->SetStatus(_ManagedMail->Stamp == 0 ? "Backend connected" : "Backend returned an error");
                            }
                            if (_ManagedMail->Stamp == 0 && !String::IsNullOrEmpty(_ManagedMail->PayloadText))
                            {
                                m_Window->SetLastPayload(_ManagedMail->PayloadText);
                            }
                        }
                    }
                    catch (const std::exception& _Error)
                    {
                        m_Window->SetStatus("Mailbox polling failed: " + _ToManagedUTF8(_Error.what()));
                    }
                    catch (Exception^ _Error)
                    {
                        m_Window->SetStatus("Mailbox polling failed: " + _Error->Message);
                    }
                }

            private:
                iCAX::Frontend::IFrontendBridge* m_pBridge = nullptr;
                Thread^ m_UIThread;
                Application^ m_Application;
                CWpfMainWindow^ m_Window;
                Dispatcher^ m_Dispatcher;
                DispatcherTimer^ m_MailTimer;
                ManualResetEventSlim^ m_Started;
                ManualResetEventSlim^ m_Closed;
                Exception^ m_StartupException;
                Dictionary<UInt64, String^>^ m_PendingCommands;
                String^ m_ApplicationChannelID;
                Int64 m_NextMailID = static_cast<Int64>(kStartupHandshakeRequestID);
                volatile bool m_Running = false;
            };

            CWpfMainWindow::CWpfMainWindow(CWpfRuntime^ Runtime_)
                : m_Runtime(Runtime_)
            {
                Title = "iCAX";
                Width = 1280;
                Height = 820;
                MinWidth = 960;
                MinHeight = 640;
                Background = gcnew SolidColorBrush(Color::FromRgb(246, 247, 249));

                auto _Root = gcnew Grid();
                _Root->RowDefinitions->Add(gcnew RowDefinition());
                _Root->RowDefinitions[0]->Height = GridLength(48);
                _Root->RowDefinitions->Add(gcnew RowDefinition());
                _Root->RowDefinitions[1]->Height = GridLength(1, GridUnitType::Star);
                _Root->RowDefinitions->Add(gcnew RowDefinition());
                _Root->RowDefinitions[2]->Height = GridLength(28);
                Content = _Root;

                auto _Toolbar = gcnew DockPanel();
                _Toolbar->LastChildFill = false;
                _Toolbar->Background = Brushes::White;
                _Toolbar->Margin = Thickness(0, 0, 0, 1);
                Grid::SetRow(_Toolbar, 0);
                _Root->Children->Add(_Toolbar);

                auto _Title = gcnew TextBlock();
                _Title->Text = "iCAX Workbench";
                _Title->FontSize = 16;
                _Title->FontWeight = FontWeights::SemiBold;
                _Title->VerticalAlignment = System::Windows::VerticalAlignment::Center;
                _Title->Margin = Thickness(16, 0, 24, 0);
                DockPanel::SetDock(_Title, Dock::Left);
                _Toolbar->Children->Add(_Title);

                _Toolbar->Children->Add(MakeToolbarButton("Refresh", gcnew RoutedEventHandler(this, &CWpfMainWindow::OnRefreshApplication)));
                _Toolbar->Children->Add(MakeToolbarButton("Products", gcnew RoutedEventHandler(this, &CWpfMainWindow::OnListProducts)));
                _Toolbar->Children->Add(MakeToolbarButton("Start", gcnew RoutedEventHandler(this, &CWpfMainWindow::OnStartProduct)));
                _Toolbar->Children->Add(MakeToolbarButton("Open", gcnew RoutedEventHandler(this, &CWpfMainWindow::OnOpenProjectFile)));
                _Toolbar->Children->Add(MakeToolbarButton("Exit", gcnew RoutedEventHandler(this, &CWpfMainWindow::OnExit)));

                auto _Body = gcnew Grid();
                _Body->Margin = Thickness(10);
                _Body->ColumnDefinitions->Add(gcnew ColumnDefinition());
                _Body->ColumnDefinitions[0]->Width = GridLength(260);
                _Body->ColumnDefinitions->Add(gcnew ColumnDefinition());
                _Body->ColumnDefinitions[1]->Width = GridLength(1, GridUnitType::Star);
                _Body->ColumnDefinitions->Add(gcnew ColumnDefinition());
                _Body->ColumnDefinitions[2]->Width = GridLength(360);
                Grid::SetRow(_Body, 1);
                _Root->Children->Add(_Body);

                auto _LeftStack = gcnew StackPanel();
                _LeftStack->Orientation = Orientation::Vertical;
                _LeftStack->Margin = Thickness(0, 0, 10, 0);
                m_ChannelText = gcnew TextBlock();
                m_ChannelText->TextWrapping = TextWrapping::Wrap;
                m_ChannelText->Foreground = Brushes::DimGray;
                _LeftStack->Children->Add(m_ChannelText);

                auto _ProjectHint = gcnew TextBlock();
                _ProjectHint->Text = "No project";
                _ProjectHint->Margin = Thickness(0, 16, 0, 0);
                _ProjectHint->FontSize = 20;
                _ProjectHint->FontWeight = FontWeights::SemiBold;
                _LeftStack->Children->Add(_ProjectHint);

                auto _LeftPanel = MakePanel("Application", _LeftStack);
                Grid::SetColumn(_LeftPanel, 0);
                _Body->Children->Add(_LeftPanel);

                auto _Viewport = gcnew Border();
                _Viewport->Background = gcnew SolidColorBrush(Color::FromRgb(30, 34, 40));
                _Viewport->BorderBrush = gcnew SolidColorBrush(Color::FromRgb(17, 20, 24));
                _Viewport->BorderThickness = Thickness(1);
                _Viewport->CornerRadius = CornerRadius(4);

                auto _ViewportText = gcnew TextBlock();
                _ViewportText->Text = "Native Viewport";
                _ViewportText->Foreground = Brushes::White;
                _ViewportText->FontSize = 22;
                _ViewportText->HorizontalAlignment = System::Windows::HorizontalAlignment::Center;
                _ViewportText->VerticalAlignment = System::Windows::VerticalAlignment::Center;
                _Viewport->Child = _ViewportText;
                Grid::SetColumn(_Viewport, 1);
                _Body->Children->Add(_Viewport);

                auto _RightGrid = gcnew Grid();
                _RightGrid->RowDefinitions->Add(gcnew RowDefinition());
                _RightGrid->RowDefinitions[0]->Height = GridLength(220);
                _RightGrid->RowDefinitions->Add(gcnew RowDefinition());
                _RightGrid->RowDefinitions[1]->Height = GridLength(1, GridUnitType::Star);
                _RightGrid->Margin = Thickness(10, 0, 0, 0);

                m_MailList = gcnew ListBox();
                m_MailList->FontFamily = gcnew System::Windows::Media::FontFamily("Consolas");
                m_MailList->FontSize = 12;
                auto _MailPanel = MakePanel("Mailbox", m_MailList);
                Grid::SetRow(_MailPanel, 0);
                _RightGrid->Children->Add(_MailPanel);

                m_PayloadText = gcnew TextBox();
                m_PayloadText->FontFamily = gcnew System::Windows::Media::FontFamily("Consolas");
                m_PayloadText->FontSize = 12;
                m_PayloadText->IsReadOnly = true;
                m_PayloadText->TextWrapping = TextWrapping::Wrap;
                m_PayloadText->VerticalScrollBarVisibility = ScrollBarVisibility::Auto;
                m_PayloadText->HorizontalScrollBarVisibility = ScrollBarVisibility::Auto;
                auto _PayloadPanel = MakePanel("Payload", m_PayloadText);
                Grid::SetRow(_PayloadPanel, 1);
                _RightGrid->Children->Add(_PayloadPanel);

                Grid::SetColumn(_RightGrid, 2);
                _Body->Children->Add(_RightGrid);

                auto _StatusBar = gcnew Border();
                _StatusBar->Background = Brushes::White;
                _StatusBar->BorderBrush = gcnew SolidColorBrush(Color::FromRgb(226, 229, 234));
                _StatusBar->BorderThickness = Thickness(0, 1, 0, 0);
                m_StatusText = gcnew TextBlock();
                m_StatusText->Text = "Starting";
                m_StatusText->VerticalAlignment = System::Windows::VerticalAlignment::Center;
                m_StatusText->Margin = Thickness(12, 0, 12, 0);
                _StatusBar->Child = m_StatusText;
                Grid::SetRow(_StatusBar, 2);
                _Root->Children->Add(_StatusBar);
            }

            Button^ CWpfMainWindow::MakeToolbarButton(String^ Text_, RoutedEventHandler^ Handler_)
            {
                auto _Button = gcnew Button();
                _Button->Content = Text_;
                _Button->MinWidth = 82;
                _Button->Height = 30;
                _Button->Margin = Thickness(0, 0, 8, 0);
                _Button->Padding = Thickness(10, 0, 10, 0);
                _Button->VerticalAlignment = System::Windows::VerticalAlignment::Center;
                _Button->Click += Handler_;
                return _Button;
            }

            Border^ CWpfMainWindow::MakePanel(String^ Header_, UIElement^ Content_)
            {
                auto _Outer = gcnew Border();
                _Outer->Background = Brushes::White;
                _Outer->BorderBrush = gcnew SolidColorBrush(Color::FromRgb(222, 226, 232));
                _Outer->BorderThickness = Thickness(1);
                _Outer->CornerRadius = CornerRadius(4);
                _Outer->Padding = Thickness(10);

                auto _Stack = gcnew DockPanel();
                auto _Header = gcnew TextBlock();
                _Header->Text = Header_;
                _Header->FontWeight = FontWeights::SemiBold;
                _Header->Margin = Thickness(0, 0, 0, 8);
                DockPanel::SetDock(_Header, Dock::Top);
                _Stack->Children->Add(_Header);
                _Stack->Children->Add(Content_);

                _Outer->Child = _Stack;
                return _Outer;
            }

            void CWpfMainWindow::SetApplicationChannelID(String^ ChannelID_)
            {
                m_ChannelText->Text = "Application channel: " + ChannelID_;
            }

            void CWpfMainWindow::SetStatus(String^ Text_)
            {
                m_StatusText->Text = Text_;
            }

            void CWpfMainWindow::AppendMail(CManagedMailEnvelope^ Mail_, String^ CommandName_)
            {
                auto _Line = String::Format(
                    "{0:HH:mm:ss.fff} {1} origin={2} {3}",
                    DateTime::Now,
                    CommandName_,
                    Mail_->OriginID,
                    _FormatStamp(Mail_->Stamp));
                m_MailList->Items->Insert(0, _Line);
                while (m_MailList->Items->Count > 200)
                {
                    m_MailList->Items->RemoveAt(m_MailList->Items->Count - 1);
                }
            }

            void CWpfMainWindow::SetLastPayload(String^ PayloadText_)
            {
                m_PayloadText->Text = PayloadText_;
            }

            void CWpfMainWindow::OnRefreshApplication(Object^, RoutedEventArgs^)
            {
                try
                {
                    m_Runtime->SendApplicationCommand("GetState", _EmptyObjectPayload());
                    SetStatus("Refreshing application state");
                }
                catch (Exception^ _Error)
                {
                    SetStatus(_Error->Message);
                }
            }

            void CWpfMainWindow::OnListProducts(Object^, RoutedEventArgs^)
            {
                try
                {
                    m_Runtime->SendApplicationCommand("ListProducts", _EmptyObjectPayload());
                    SetStatus("Listing products");
                }
                catch (Exception^ _Error)
                {
                    SetStatus(_Error->Message);
                }
            }

            void CWpfMainWindow::OnStartProduct(Object^, RoutedEventArgs^)
            {
                try
                {
                    m_Runtime->SendApplicationCommand("StartProduct", _EmptyObjectPayload());
                    SetStatus("Starting product");
                }
                catch (Exception^ _Error)
                {
                    SetStatus(_Error->Message);
                }
            }

            void CWpfMainWindow::OnOpenProjectFile(Object^, RoutedEventArgs^)
            {
                try
                {
                    m_Runtime->OpenProjectFile();
                    SetStatus("Opening project");
                }
                catch (Exception^ _Error)
                {
                    SetStatus(_Error->Message);
                }
            }

            void CWpfMainWindow::OnExit(Object^, RoutedEventArgs^)
            {
                Close();
            }
        }
    }
}

class iCAX::Frontend::CWpfUIContainer::Impl final
{
public:
    mutable std::mutex Mutex;
    CUIContainerConfig Config;
    msclr::gcroot<iCAX::Frontend::Wpf::CWpfRuntime^> Runtime;
    bool bRunning = false;
};

iCAX::Frontend::CWpfUIContainer::CWpfUIContainer()
    : m_pImpl(std::make_unique<Impl>())
{
    m_pImpl->Runtime = gcnew Wpf::CWpfRuntime();
}

iCAX::Frontend::CWpfUIContainer::~CWpfUIContainer()
{
    Stop();
}

void iCAX::Frontend::CWpfUIContainer::SetConfig(IN const CUIContainerConfig& Config_)
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    if (m_pImpl->bRunning)
    {
        throw std::logic_error("WpfUIContainer config cannot be changed after start");
    }
    m_pImpl->Config = Config_;
}

void iCAX::Frontend::CWpfUIContainer::Start()
{
    CUIContainerConfig _Config;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        if (m_pImpl->bRunning)
        {
            return;
        }
        _Config = m_pImpl->Config;
    }

    try
    {
        m_pImpl->Runtime->Start(_Config.pFrontendBridge, _Config.nStartupHandshakeTimeoutMS);
    }
    catch (Exception^ _Error)
    {
        throw std::runtime_error(_ToNativeUTF8(_Error->ToString()));
    }

    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->bRunning = true;
}

void iCAX::Frontend::CWpfUIContainer::Stop()
{
    bool _bShouldStop = false;
    {
        std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
        _bShouldStop = m_pImpl->bRunning;
        m_pImpl->bRunning = false;
    }

    if (_bShouldStop)
    {
        m_pImpl->Runtime->Stop();
        m_pImpl->Runtime->WaitForExit();
    }
}

void iCAX::Frontend::CWpfUIContainer::WaitForExit()
{
    m_pImpl->Runtime->WaitForExit();
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    m_pImpl->bRunning = false;
}

bool iCAX::Frontend::CWpfUIContainer::IsRunning() const
{
    std::lock_guard<std::mutex> _Lock(m_pImpl->Mutex);
    return m_pImpl->bRunning && m_pImpl->Runtime->IsRunning();
}

namespace
{
    iCAX::Frontend::IUIContainer* __cdecl _CreateWpfUIContainer()
    {
        return new iCAX::Frontend::CWpfUIContainer();
    }

    void __cdecl _DestroyWpfUIContainer(IN iCAX::Frontend::IUIContainer* pContainer_)
    {
        delete static_cast<iCAX::Frontend::CWpfUIContainer*>(pContainer_);
    }
}

extern "C" __declspec(dllexport) bool __cdecl ICAXRegisterUIContainerModule()
{
    static bool _bRegistered = false;
    if (_bRegistered)
    {
        return true;
    }

    (void)iCAX::Frontend::CUIContainerFactory::Register(
        iCAX::Frontend::CUIContainerRegistration{
            "wpf",
            &_CreateWpfUIContainer,
            &_DestroyWpfUIContainer,
            nullptr });
    _bRegistered = true;
    return true;
}
