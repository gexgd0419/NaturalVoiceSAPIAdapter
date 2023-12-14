// dllmain.h: 模块类的声明。

class CNaturalVoiceSAPIAdapterModule : public ATL::CAtlDllModuleT< CNaturalVoiceSAPIAdapterModule >
{
public :
	DECLARE_LIBID(LIBID_NaturalVoiceSAPIAdapterLib)
	DECLARE_REGISTRY_APPID_RESOURCEID(IDR_NATURALVOICESAPIADAPTER, "{19d3bf0c-aad3-4348-8fc3-bd439f0da852}")
};

extern class CNaturalVoiceSAPIAdapterModule _AtlModule;
