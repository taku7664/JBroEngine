#pragma once

#include <cstdint>

class CProjectManager;
class IRHITexture;

class CImEditor : public CModule
{
public:
	CImEditor();
	virtual ~CImEditor();

public:
	void BeginFrame();
	void EndFrame();
	void Update();

public:
	template<typename T>
	SafePtr<T>			CreateImWindow(const char* key, ImGuiID parentId = 0);
	void				DestroyImWindow(ImGuiID id);
	SafePtr<IImWindow>  FindImWindow(ImGuiID id);
	template<typename T>
	SafePtr<T>			FindImWindow(ImGuiID id);

	void OpenPopup(const ImPopupDesc& desc);
	SafePtr<CProjectManager> GetProjectManager() const;
	void RequestSceneViewRenderTarget(std::uint32_t width, std::uint32_t height);
	SafePtr<IRHITexture> GetSceneViewRenderTarget() const;
	void* GetSceneViewTextureID() const;

private:
	void OnPreInitialize() override;
	void OnPostInitialize() override;
	void OnPreFinalize() override;
	void OnPostFinalize() override;
	void OnBeginFrame() override;
	void OnUpdate() override;
	void OnPrepareRender() override;
	void OnRender() override;

private:
	bool InitializeImGui();
	void FinalizeImGui();

	bool DestroyImWindowEx(ImGuiID id);

private:
	ImGuiContext* m_imguiContext;
	bool m_isWin32BackendInitialized = false;
	bool m_isDX11BackendInitialized = false;

	std::unordered_map<ImGuiID, OwnerPtr<CImWindow>> m_imWindowTable;
	std::vector<CImWindow*>		m_imWindowVector;
	std::queue<CImPopupWindow>	m_imPopupWindowQueue;

	std::queue<std::function<void()>> m_delayEventQueue;
	OwnerPtr<CProjectManager> m_projectManager;
	OwnerPtr<IRHITexture> m_sceneViewRenderTarget;
	std::uint32_t m_sceneViewWidth = 0;
	std::uint32_t m_sceneViewHeight = 0;
	bool m_sceneViewRequested = false;
};

template<typename T>
inline SafePtr<T> CImEditor::CreateImWindow(const char* key, ImGuiID parentId)
{
	static_assert(std::is_base_of_v<CImWindow, T>, "T must derive from CImWindow.");

	if (nullptr == key)
	{
		return nullptr;
	}

	ImGuiID hashedID = ImHashStr(key);
	if (false == m_imWindowTable.contains(hashedID))
	{
		OwnerPtr<T> newWindow = MakeOwnerPtr<T>(hashedID, parentId);
		SafePtr<T> result = newWindow.GetSafePtr();
		m_imWindowTable[hashedID] = std::move(newWindow);
		if (CImDockWindow* parent = dynamic_cast<CImDockWindow*>(FindImWindow(parentId).TryGet()))
		{
			parent->AddChildImWindow(result);
		}
		result->Initialize();
		m_imWindowVector.push_back(result.TryGet());
		return result;
	}
	return nullptr;
}

template<typename T>
inline SafePtr<T> CImEditor::FindImWindow(ImGuiID id)
{
	static_assert(std::is_base_of_v<IImWindow, T>, "T must derive from IImWindow.");
	return DynamicSafePtrCast<T>(FindImWindow(id));
}
