#pragma once

class CEngine;

class CApplication
{
public:
	CApplication() = default;
	virtual ~CApplication() = default;
	CApplication(const CApplication&) = delete;
	CApplication& operator=(const CApplication&) = delete;
	CApplication(CApplication&&) = delete;
	CApplication& operator=(CApplication&&) = delete;

public:
	virtual void OnPreInitialize() = 0;
	virtual void OnPostInitialize() = 0;
	virtual void OnPreTick() {}
	virtual void OnPostTick() {}
	virtual void OnPreFinalize() = 0;
	virtual void OnPostFinalize() = 0;

public:
	bool InitializeApplication();
	bool TickApplication();
	void FinalizeApplication();

	void Quit();
	void Run();
	void RunNative();

protected:
	CEngine* GetEngine();
	const CEngine* GetEngine() const;

private:
	bool m_isRunning = false;
	bool m_isInitialized = false;
	OwnerPtr<CEngine> m_engine;
};
