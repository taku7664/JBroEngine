#pragma once

// 모듈은 엔진 서비스에 전역 `Engine`(EngineCore) 으로 직접 접근한다. 모듈마다 EngineCore
// 포인터를 따로 들고 다니지 않는다 — extern 전역이므로 어디서든 `Engine.X` 로 쓰면 된다.
class CModule
{
	friend class CEngine;
public:
	CModule() = default;
	virtual ~CModule() = default;
	CModule(const CModule&) = delete;
	CModule& operator=(const CModule&) = delete;
	CModule(CModule&&) = delete;
	CModule& operator=(CModule&&) = delete;

private:
	void Initialize(const char* moduleName);
	void Finalize();
	void BeginFrame();
	void Update();
	void PrepareRender();
	void Render();
	void EndFrame();

	virtual void OnPreInitialize() {};
	virtual void OnPostInitialize() {};
	virtual void OnPreFinalize() {};
	virtual void OnPostFinalize() {};
	virtual void OnBeginFrame() {}
	virtual void OnUpdate() {}
	virtual void OnPrepareRender() {}
	virtual void OnRender() {}
	virtual void OnEndFrame() {}

private:
	std::string m_moduleName;
};

