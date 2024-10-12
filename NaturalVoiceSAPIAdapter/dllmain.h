// dllmain.h: 模块类的声明。

class CNaturalVoiceSAPIAdapterModule : public ATL::CAtlDllModuleT< CNaturalVoiceSAPIAdapterModule >
{
public :
	DECLARE_LIBID(LIBID_NaturalVoiceSAPIAdapterLib)
    static LPCOLESTR GetAppId() throw()
    {
        return L"{19d3bf0c-aad3-4348-8fc3-bd439f0da852}";
    }
    static const TCHAR* GetAppIdT() throw()
    {
        return L"{19d3bf0c-aad3-4348-8fc3-bd439f0da852}";
    }
    static HRESULT __stdcall UpdateRegistryAppId(BOOL bRegister) throw()
    {
        ATL::_ATL_REGMAP_ENTRY aMapEntries[] = { { L"APPID", GetAppId() }, { L"ModulePath", g_regModulePath }, { 0, 0 } };
        return ATL::_pAtlModule->UpdateRegistryFromResource(101, bRegister, aMapEntries);
    }
};

extern class CNaturalVoiceSAPIAdapterModule _AtlModule;
