#pragma once

#include "Utillity/SafePtr.h"

class CSceneManager;
class CTime;
class CInput;
class CFileSystem;
class CThreadService;
class CReflectionRegistry;

struct Core
{
	inline static SafePtr<CTime> Time;
	inline static SafePtr<CInput> Input;
	inline static SafePtr<CSceneManager> SceneManager;
	inline static SafePtr<CFileSystem> FileSystem;
	inline static SafePtr<CThreadService> Thread;
	inline static SafePtr<CReflectionRegistry> Reflection;
};
