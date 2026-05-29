#pragma once

struct EngineCore;

class CModule abstract
{
	friend class CEngine;
public:
	CModule() = default;
	virtual ~CModule() = default;
	CModule(const CModule&) = delete;
	CModule& operator=(const CModule&) = delete;
	CModule(CModule&&) = delete;
	CModule& operator=(CModule&&) = delete;

protected:
	const EngineCore* GetEngineCore() const;

private:
	void Initialize(const char* moduleName, const EngineCore& engineCore);
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
	const EngineCore* m_engineCore = nullptr;
};

