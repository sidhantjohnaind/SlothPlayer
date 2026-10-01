#include "SMTCManager.h"
#include <iostream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <unknwn.h>
#include <inspectable.h>

// Basic WinRT types
typedef struct HSTRING__* HSTRING;
typedef struct EventRegistrationToken {
    __int64 value;
} EventRegistrationToken;

enum MediaPlaybackStatus {
    MediaPlaybackStatus_Closed = 0,
    MediaPlaybackStatus_Changing = 1,
    MediaPlaybackStatus_Stopped = 2,
    MediaPlaybackStatus_Playing = 3,
    MediaPlaybackStatus_Paused = 4
};

enum MediaPlaybackType {
    MediaPlaybackType_Unknown = 0,
    MediaPlaybackType_Music = 1,
    MediaPlaybackType_Video = 2,
    MediaPlaybackType_Image = 3
};

enum SystemMediaTransportControlsButton {
    SystemMediaTransportControlsButton_Play = 0,
    SystemMediaTransportControlsButton_Pause = 1,
    SystemMediaTransportControlsButton_Stop = 2,
    SystemMediaTransportControlsButton_Record = 3,
    SystemMediaTransportControlsButton_FastForward = 4,
    SystemMediaTransportControlsButton_Rewind = 5,
    SystemMediaTransportControlsButton_Next = 6,
    SystemMediaTransportControlsButton_Previous = 7,
    SystemMediaTransportControlsButton_ChannelUp = 8,
    SystemMediaTransportControlsButton_ChannelDown = 9
};

// GUID definitions
static const IID IID_ISystemMediaTransportControlsInterop = 
    { 0xddb0472d, 0xc911, 0x4a1f, { 0x86, 0xd9, 0xdc, 0x3d, 0x71, 0xa9, 0x5f, 0x5a } };

static const IID IID_ISystemMediaTransportControls = 
    { 0x99fa3ff4, 0x1742, 0x42a6, { 0x90, 0x2e, 0x08, 0x7d, 0x41, 0xf9, 0x65, 0xec } };

static const IID IID_ISystemMediaTransportControlsDisplayUpdater = 
    { 0x85564170, 0xd581, 0x421a, { 0x9b, 0x32, 0x5c, 0xb8, 0x38, 0xcb, 0x9c, 0x31 } };

static const IID IID_IMusicDisplayProperties = 
    { 0xcb53d262, 0x7348, 0x41f3, { 0xb1, 0x6e, 0xa6, 0x63, 0x65, 0x61, 0x51, 0x6d } };

static const IID IID_IMusicDisplayProperties2 = 
    { 0x00368462, 0x9770, 0x4703, { 0xac, 0xac, 0x02, 0x0e, 0x85, 0x43, 0x35, 0x52 } };

static const IID IID_ISystemMediaTransportControlsButtonPressedEventArgs = 
    { 0xb7f47116, 0xa56f, 0x4dc8, { 0x9e, 0x11, 0x92, 0x03, 0x1f, 0x4a, 0x87, 0xc2 } };

static const IID IID_ISMTCButtonPressedHandler = 
    { 0x0557e996, 0x7b23, 0x5bae, { 0xaa, 0x81, 0xea, 0x0d, 0x67, 0x11, 0x43, 0xa4 } };

static inline bool isGuidEqual(REFIID a, REFIID b) {
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}

enum AsyncStatus {
    AsyncStatus_Started = 0,
    AsyncStatus_Completed = 1,
    AsyncStatus_Canceled = 2,
    AsyncStatus_Error = 3
};

static const IID IID_IAsyncInfo = 
    { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

static const IID IID_IStorageFileStatics = 
    { 0x5984c710, 0xdaf2, 0x43c8, { 0x8b, 0xb4, 0xa4, 0xd3, 0xea, 0xcf, 0xd0, 0x3f } };

static const IID IID_IRandomAccessStreamReferenceStatics = 
    { 0x857309dc, 0x3fbf, 0x4e7d, { 0x98, 0x6f, 0xef, 0x3b, 0x1a, 0x07, 0xa9, 0x64 } };

// Storage & Async COM Interface definitions
struct IStorageFile : public IInspectable {};

struct IAsyncOperation_StorageFile : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IStorageFile** results) = 0;
};

struct IAsyncInfo : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(AsyncStatus* status) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

struct IStorageFileStatics : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetFileFromPathAsync(HSTRING path, IAsyncOperation_StorageFile** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFileFromApplicationUriAsync(void* uri, IAsyncOperation_StorageFile** operation) = 0;
};

struct IRandomAccessStreamReference : public IInspectable {};

struct IRandomAccessStreamReferenceStatics : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFromFile(IStorageFile* file, IRandomAccessStreamReference** streamReference) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromUri(void* uri, IRandomAccessStreamReference** streamReference) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateFromStream(void* stream, IRandomAccessStreamReference** streamReference) = 0;
};

// COM Interface definitions
struct ISystemMediaTransportControlsButtonPressedEventArgs : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Button(SystemMediaTransportControlsButton* value) = 0;
};

struct ISMTCButtonPressedHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(void* sender, ISystemMediaTransportControlsButtonPressedEventArgs* args) = 0;
};

struct IMusicDisplayProperties : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Title(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Title(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AlbumArtist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumArtist(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Artist(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Artist(HSTRING value) = 0;
};

struct IMusicDisplayProperties2 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AlbumTitle(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AlbumTitle(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_TrackNumber(UINT32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_TrackNumber(UINT32 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Genres(void** value) = 0;
};

struct ISystemMediaTransportControlsDisplayUpdater : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Type(MediaPlaybackType* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Type(MediaPlaybackType value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AppMediaId(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AppMediaId(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Thumbnail(void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Thumbnail(void* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MusicProperties(IMusicDisplayProperties** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_VideoProperties(void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ImageProperties(void** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE CopyFromFileAsync(MediaPlaybackType type, void* source, void** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearAll() = 0;
    virtual HRESULT STDMETHODCALLTYPE Update() = 0;
};

struct ISystemMediaTransportControls : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_PlaybackStatus(MediaPlaybackStatus* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_PlaybackStatus(MediaPlaybackStatus value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayUpdater(ISystemMediaTransportControlsDisplayUpdater** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SoundLevel(void* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPlayEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPlayEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStopEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStopEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPauseEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPauseEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRecordEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRecordEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsFastForwardEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsFastForwardEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRewindEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsRewindEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPreviousEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsPreviousEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsNextEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsNextEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelUpEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelUpEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsChannelDownEnabled(boolean* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsChannelDownEnabled(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_ButtonPressed(ISMTCButtonPressedHandler* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_ButtonPressed(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_PropertyChanged(void* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PropertyChanged(EventRegistrationToken token) = 0;
};

struct ISystemMediaTransportControlsInterop : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND appWindow, REFIID riid, void** mediaTransportControl) = 0;
};

// Dynamic combase loaders
typedef HRESULT (WINAPI *pfnRoInitialize)(UINT32);
typedef HRESULT (WINAPI *pfnRoGetActivationFactory)(HSTRING, REFIID, void**);
typedef HRESULT (WINAPI *pfnWindowsCreateString)(PCWSTR, UINT32, HSTRING*);
typedef HRESULT (WINAPI *pfnWindowsDeleteString)(HSTRING);

static pfnRoInitialize g_pRoInitialize = nullptr;
static pfnRoGetActivationFactory g_pRoGetActivationFactory = nullptr;
static pfnWindowsCreateString g_pWindowsCreateString = nullptr;
static pfnWindowsDeleteString g_pWindowsDeleteString = nullptr;
static bool g_combaseLoaded = false;

static bool loadCombase() {
    if (g_combaseLoaded) return (g_pRoGetActivationFactory && g_pWindowsCreateString && g_pWindowsDeleteString);
    g_combaseLoaded = true;

    HMODULE hMod = LoadLibraryW(L"combase.dll");
    if (!hMod) return false;

    g_pRoInitialize = (pfnRoInitialize)(void*)GetProcAddress(hMod, "RoInitialize");
    g_pRoGetActivationFactory = (pfnRoGetActivationFactory)(void*)GetProcAddress(hMod, "RoGetActivationFactory");
    g_pWindowsCreateString = (pfnWindowsCreateString)(void*)GetProcAddress(hMod, "WindowsCreateString");
    g_pWindowsDeleteString = (pfnWindowsDeleteString)(void*)GetProcAddress(hMod, "WindowsDeleteString");

    if (g_pRoInitialize) {
        // RO_INIT_MULTITHREADED = 1
        g_pRoInitialize(1);
    }

    return (g_pRoGetActivationFactory && g_pWindowsCreateString && g_pWindowsDeleteString);
}

// Custom TypedEventHandler COM implementation for ButtonPressed
class SMTCButtonEventHandler : public ISMTCButtonPressedHandler {
public:
    SMTCButtonEventHandler(const SMTCCallbacks& callbacks) : m_callbacks(callbacks), m_refCount(1) {}
    virtual ~SMTCButtonEventHandler() {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (isGuidEqual(riid, IID_IUnknown) ||
            isGuidEqual(riid, IID_ISMTCButtonPressedHandler) ||
            isGuidEqual(riid, IID_IAgileObject)) {
            *ppv = static_cast<ISMTCButtonPressedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG res = InterlockedDecrement(&m_refCount);
        if (res == 0) delete this;
        return res;
    }

    HRESULT STDMETHODCALLTYPE Invoke(void* sender, ISystemMediaTransportControlsButtonPressedEventArgs* args) override {
        (void)sender;
        if (!args) return S_OK;

        SystemMediaTransportControlsButton btn = SystemMediaTransportControlsButton_Play;
        HRESULT hr = args->get_Button(&btn);
        if (FAILED(hr)) {
            ISystemMediaTransportControlsButtonPressedEventArgs* typedArgs = nullptr;
            if (SUCCEEDED(args->QueryInterface(IID_ISystemMediaTransportControlsButtonPressedEventArgs, (void**)&typedArgs)) && typedArgs) {
                hr = typedArgs->get_Button(&btn);
                typedArgs->Release();
            }
        }

        if (SUCCEEDED(hr)) {
            switch (btn) {
                case SystemMediaTransportControlsButton_Play:
                    if (m_callbacks.onPlay) m_callbacks.onPlay();
                    else if (m_callbacks.onTogglePlayPause) m_callbacks.onTogglePlayPause();
                    break;
                case SystemMediaTransportControlsButton_Pause:
                    if (m_callbacks.onPause) m_callbacks.onPause();
                    else if (m_callbacks.onTogglePlayPause) m_callbacks.onTogglePlayPause();
                    break;
                case SystemMediaTransportControlsButton_Next:
                    if (m_callbacks.onNext) m_callbacks.onNext();
                    break;
                case SystemMediaTransportControlsButton_Previous:
                    if (m_callbacks.onPrevious) m_callbacks.onPrevious();
                    break;
                case SystemMediaTransportControlsButton_Stop:
                    if (m_callbacks.onStop) m_callbacks.onStop();
                    break;
                default:
                    break;
            }
        }
        return S_OK;
    }

private:
    SMTCCallbacks m_callbacks;
    LONG m_refCount;
};

struct SMTCManager::Impl {
    HWND hWnd = nullptr;
    SMTCCallbacks callbacks;
    ISystemMediaTransportControls* smtc = nullptr;
    ISystemMediaTransportControlsDisplayUpdater* updater = nullptr;
    EventRegistrationToken buttonToken = {0};
    bool initialized = false;
};

#else
struct SMTCManager::Impl {
    bool initialized = false;
};
#endif

SMTCManager& SMTCManager::instance() {
    static SMTCManager s_instance;
    return s_instance;
}

SMTCManager::SMTCManager() : m_impl(new Impl()) {}

SMTCManager::~SMTCManager() {
    shutdown();
    delete m_impl;
}

bool SMTCManager::initialize(void* hWnd, const SMTCCallbacks& callbacks) {
#ifdef _WIN32
    if (!hWnd) return false;
    if (!loadCombase()) return false;

    m_impl->hWnd = static_cast<HWND>(hWnd);
    m_impl->callbacks = callbacks;

    // Set AppUserModelID so Windows 10/11 Shell and Action Center recognize SlothPlayer
    typedef HRESULT (WINAPI *pfnSetCurrentProcessExplicitAppUserModelID)(PCWSTR);
    HMODULE hShell = LoadLibraryW(L"shell32.dll");
    if (hShell) {
        auto setAppID = (pfnSetCurrentProcessExplicitAppUserModelID)GetProcAddress(hShell, "SetCurrentProcessExplicitAppUserModelID");
        if (setAppID) {
            setAppID(L"SlothPlayer.App");
        }
    }

    // 1. Get ActivationFactory for SystemMediaTransportControls
    HSTRING hClassName = nullptr;
    const wchar_t* className = L"Windows.Media.SystemMediaTransportControls";
    if (FAILED(g_pWindowsCreateString(className, static_cast<UINT32>(wcslen(className)), &hClassName))) {
        std::cerr << "[SMTC] WindowsCreateString failed!" << std::endl;
        return false;
    }

    ISystemMediaTransportControlsInterop* interop = nullptr;
    HRESULT hr = g_pRoGetActivationFactory(hClassName, IID_ISystemMediaTransportControlsInterop, (void**)&interop);
    g_pWindowsDeleteString(hClassName);

    if (FAILED(hr) || !interop) {
        std::cerr << "[SMTC] RoGetActivationFactory failed: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // 2. Obtain ISystemMediaTransportControls for our HWND
    hr = interop->GetForWindow(m_impl->hWnd, IID_ISystemMediaTransportControls, (void**)&m_impl->smtc);
    interop->Release();

    if (FAILED(hr) || !m_impl->smtc) {
        std::cerr << "[SMTC] GetForWindow failed: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // 3. Configure controls & enable standard playback transport buttons
    m_impl->smtc->put_IsEnabled(TRUE);
    m_impl->smtc->put_IsPlayEnabled(TRUE);
    m_impl->smtc->put_IsPauseEnabled(TRUE);
    m_impl->smtc->put_IsNextEnabled(TRUE);
    m_impl->smtc->put_IsPreviousEnabled(TRUE);
    m_impl->smtc->put_IsStopEnabled(TRUE);
    m_impl->smtc->put_PlaybackStatus(MediaPlaybackStatus_Closed);

    // 4. Retrieve DisplayUpdater
    m_impl->smtc->get_DisplayUpdater(&m_impl->updater);
    if (m_impl->updater) {
        m_impl->updater->put_Type(MediaPlaybackType_Music);
    }

    // 5. Register ButtonPressed event handler
    SMTCButtonEventHandler* handler = new SMTCButtonEventHandler(callbacks);
    hr = m_impl->smtc->add_ButtonPressed(handler, &m_impl->buttonToken);
    handler->Release();
    std::cout << "[SMTC] add_ButtonPressed hr: 0x" << std::hex << hr << std::dec << std::endl;

    m_impl->initialized = (m_impl->smtc != nullptr && m_impl->updater != nullptr);
    if (m_impl->initialized) {
        updateTrack("No track playing", "SlothPlayer", "");
    }
    std::cout << "[SMTC] Initialized: " << (m_impl->initialized ? "SUCCESS" : "FAILED") << std::endl;
    return m_impl->initialized;
#else
    (void)hWnd; (void)callbacks;
    return false;
#endif
}

void SMTCManager::shutdown() {
#ifdef _WIN32
    if (!m_impl || !m_impl->initialized) return;

    if (m_impl->smtc) {
        if (m_impl->buttonToken.value != 0) {
            m_impl->smtc->remove_ButtonPressed(m_impl->buttonToken);
            m_impl->buttonToken.value = 0;
        }
        m_impl->smtc->put_IsEnabled(FALSE);
        m_impl->smtc->Release();
        m_impl->smtc = nullptr;
    }

    if (m_impl->updater) {
        m_impl->updater->Release();
        m_impl->updater = nullptr;
    }

    m_impl->initialized = false;
#endif
}

void SMTCManager::updateTrack(const std::string& title, const std::string& artist, const std::string& album, const std::string& coverArtPath) {
#ifdef _WIN32
    (void)coverArtPath;
    if (!m_impl || !m_impl->initialized || !m_impl->updater) return;

    m_impl->updater->put_Type(MediaPlaybackType_Music);

    IMusicDisplayProperties* music = nullptr;
    if (SUCCEEDED(m_impl->updater->get_MusicProperties(&music)) && music) {
        auto toWString = [](const std::string& utf8) -> std::wstring {
            if (utf8.empty()) return L"";
            int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
            if (len <= 0) return L"";
            std::wstring wide(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], len);
            if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
            return wide;
        };

        std::wstring wTitle = toWString(title.empty() ? "Unknown Title" : title);
        std::wstring wArtist = toWString(artist.empty() ? "Unknown Artist" : artist);
        std::wstring wAlbum = toWString(album.empty() ? "Unknown Album" : album);

        HSTRING hTitle = nullptr, hArtist = nullptr, hAlbum = nullptr;
        g_pWindowsCreateString(wTitle.c_str(), static_cast<UINT32>(wTitle.length()), &hTitle);
        g_pWindowsCreateString(wArtist.c_str(), static_cast<UINT32>(wArtist.length()), &hArtist);
        g_pWindowsCreateString(wAlbum.c_str(), static_cast<UINT32>(wAlbum.length()), &hAlbum);

        if (hTitle) { music->put_Title(hTitle); g_pWindowsDeleteString(hTitle); }
        if (hArtist) { music->put_Artist(hArtist); g_pWindowsDeleteString(hArtist); }
        if (hAlbum) { music->put_AlbumArtist(hAlbum); }

        // Also try IMusicDisplayProperties2 for AlbumTitle if supported
        IMusicDisplayProperties2* music2 = nullptr;
        if (SUCCEEDED(music->QueryInterface(IID_IMusicDisplayProperties2, (void**)&music2)) && music2) {
            if (hAlbum) music2->put_AlbumTitle(hAlbum);
            music2->Release();
        }

        if (hAlbum) g_pWindowsDeleteString(hAlbum);
        music->Release();
    }

    // Update thumbnail artwork using StorageFile and CreateFromFile (standard for Win32 SMTC)
    bool thumbnailSet = false;
    if (!coverArtPath.empty()) {
        try {
            std::filesystem::path p(coverArtPath);
            std::error_code ec;
            if (std::filesystem::exists(p, ec)) {
                std::wstring wPath = std::filesystem::absolute(p, ec).wstring();

                HSTRING hStorageFileClass = nullptr;
                const wchar_t* storageFileClassName = L"Windows.Storage.StorageFile";
                if (SUCCEEDED(g_pWindowsCreateString(storageFileClassName, static_cast<UINT32>(wcslen(storageFileClassName)), &hStorageFileClass))) {
                    IStorageFileStatics* fileStatics = nullptr;
                    if (SUCCEEDED(g_pRoGetActivationFactory(hStorageFileClass, IID_IStorageFileStatics, (void**)&fileStatics)) && fileStatics) {
                        HSTRING hFilePath = nullptr;
                        if (SUCCEEDED(g_pWindowsCreateString(wPath.c_str(), static_cast<UINT32>(wPath.length()), &hFilePath))) {
                            IAsyncOperation_StorageFile* asyncOp = nullptr;
                            if (SUCCEEDED(fileStatics->GetFileFromPathAsync(hFilePath, &asyncOp)) && asyncOp) {
                                IAsyncInfo* asyncInfo = nullptr;
                                if (SUCCEEDED(asyncOp->QueryInterface(IID_IAsyncInfo, (void**)&asyncInfo)) && asyncInfo) {
                                    AsyncStatus status = AsyncStatus_Started;
                                    for (int retry = 0; retry < 50; ++retry) {
                                        asyncInfo->get_Status(&status);
                                        if (status != AsyncStatus_Started) break;
                                        Sleep(5);
                                    }
                                    if (status == AsyncStatus_Completed) {
                                        IStorageFile* storageFile = nullptr;
                                        if (SUCCEEDED(asyncOp->GetResults(&storageFile)) && storageFile) {
                                            HSTRING hStreamRefClass = nullptr;
                                            const wchar_t* streamRefName = L"Windows.Storage.Streams.RandomAccessStreamReference";
                                            if (SUCCEEDED(g_pWindowsCreateString(streamRefName, static_cast<UINT32>(wcslen(streamRefName)), &hStreamRefClass))) {
                                                IRandomAccessStreamReferenceStatics* streamRefFactory = nullptr;
                                                if (SUCCEEDED(g_pRoGetActivationFactory(hStreamRefClass, IID_IRandomAccessStreamReferenceStatics, (void**)&streamRefFactory)) && streamRefFactory) {
                                                    IRandomAccessStreamReference* streamRef = nullptr;
                                                    if (SUCCEEDED(streamRefFactory->CreateFromFile(storageFile, &streamRef)) && streamRef) {
                                                        m_impl->updater->put_Thumbnail(streamRef);
                                                        streamRef->Release();
                                                        thumbnailSet = true;
                                                    }
                                                    streamRefFactory->Release();
                                                }
                                                g_pWindowsDeleteString(hStreamRefClass);
                                            }
                                            storageFile->Release();
                                        }
                                    }
                                    asyncInfo->Release();
                                }
                                asyncOp->Release();
                            }
                            g_pWindowsDeleteString(hFilePath);
                        }
                        fileStatics->Release();
                    }
                    g_pWindowsDeleteString(hStorageFileClass);
                }
            }
        } catch (...) {}
    }

    if (!thumbnailSet) {
        m_impl->updater->put_Thumbnail(nullptr);
    }

    std::cout << "[SMTC] updateTrack: '" << title << "' by '" << artist << "' (art: " << (thumbnailSet ? coverArtPath : "none") << ")" << std::endl;
    HRESULT hrUp = m_impl->updater->Update();
    std::cout << "[SMTC] updater->Update() hr: 0x" << std::hex << hrUp << std::dec << std::endl;
#else
    (void)title; (void)artist; (void)album; (void)coverArtPath;
#endif
}

void SMTCManager::setPlaybackState(bool isPlaying, bool isPaused) {
#ifdef _WIN32
    if (!m_impl || !m_impl->initialized || !m_impl->smtc) return;

    MediaPlaybackStatus status;
    if (isPlaying) {
        status = MediaPlaybackStatus_Playing;
    } else if (isPaused) {
        status = MediaPlaybackStatus_Paused;
    } else {
        status = MediaPlaybackStatus_Stopped;
    }

    m_impl->smtc->put_IsEnabled(TRUE);
    m_impl->smtc->put_IsPlayEnabled(TRUE);
    m_impl->smtc->put_IsPauseEnabled(TRUE);
    m_impl->smtc->put_IsNextEnabled(TRUE);
    m_impl->smtc->put_IsPreviousEnabled(TRUE);
    m_impl->smtc->put_IsStopEnabled(TRUE);
    m_impl->smtc->put_PlaybackStatus(status);
#else
    (void)isPlaying; (void)isPaused;
#endif
}

void SMTCManager::clear() {
#ifdef _WIN32
    if (!m_impl || !m_impl->initialized || !m_impl->updater) return;
    m_impl->updater->ClearAll();
    m_impl->updater->Update();
    if (m_impl->smtc) {
        m_impl->smtc->put_PlaybackStatus(MediaPlaybackStatus_Closed);
    }
#endif
}
