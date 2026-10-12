#pragma once
#include <windows.h>
#include <vector>

// Game Controllers panel (joy.cpl) property sheet interface. Not in the current SDK (it was in the old DDK);
// the IDs and layouts match what Windows' joy.cpl uses.

// {3E6339B0-8FEE-49ED-94FD-20548C197EB7}
DEFINE_GUID(CLSID_SaitekFFConfig, 0x3e6339b0, 0x8fee, 0x49ed, 0x94, 0xfd, 0x20, 0x54, 0x8c, 0x19, 0x7e, 0xb7);
// Windows' default game controller property sheet (gcdef.dll) with the Test and Settings pages
DEFINE_GUID(CLSID_GameControllerDefaultSheet, 0x92187326, 0x72b4, 0x11d0, 0xa1, 0xac, 0x00, 0x00, 0xf8, 0x02, 0x69, 0x77);
DEFINE_GUID(IID_IDIGameCntrlPropSheet, 0x7854fb22, 0x8ee3, 0x11d0, 0xa1, 0xac, 0x00, 0x00, 0xf8, 0x02, 0x69, 0x77);

struct DIGCSHEETINFO {
    DWORD dwSize;
    USHORT nNumPages;
    LPWSTR lpwszSheetCaption;
    BOOL fSheetIconFlag;
    LPWSTR lpwszSheetIcon;
};

struct DIGCPAGEINFO {
    DWORD dwSize;
    LPWSTR lpwszPageTitle;
    DLGPROC fpPageProc;
    BOOL fProcFlag;
    DLGPROC fpPrePostProc;
    BOOL fIconFlag;
    LPWSTR lpwszPageIcon;
    LPWSTR lpwszTemplate;
    LPARAM lParam;
    HINSTANCE hInstance;
};

struct IDIGameCntrlPropSheet : public IUnknown {
    STDMETHOD(GetSheetInfo)(DIGCSHEETINFO** sheet) PURE;
    STDMETHOD(GetPageInfo)(DIGCPAGEINFO** pages) PURE;
    STDMETHOD(SetID)(USHORT id) PURE;
    STDMETHOD_(USHORT, GetID)() PURE;
};

// Property sheet for the stick: Windows' default pages plus our Force Feedback page
class PropSheet : public IDIGameCntrlPropSheet {
public:
    explicit PropSheet(HINSTANCE module);
    virtual ~PropSheet();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetSheetInfo(DIGCSHEETINFO** sheet) override;
    STDMETHODIMP GetPageInfo(DIGCPAGEINFO** pages) override;
    STDMETHODIMP SetID(USHORT id) override;
    STDMETHODIMP_(USHORT) GetID() override { return id_; }

private:
    void Build();

    LONG refs_ = 1;
    HINSTANCE module_;
    USHORT id_ = 0;
    IDIGameCntrlPropSheet* defaultSheet_ = nullptr;
    bool built_ = false;
    DIGCSHEETINFO sheet_ = {};
    std::vector<DIGCPAGEINFO> pages_;
};
