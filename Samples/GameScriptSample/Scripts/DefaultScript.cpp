#include "pch.h"
#include "DefaultScript.h"

void CDefaultScript::OnCreate()
{
	if (Engine.Debug)
	{
		Engine.Debug->Log("CDefaultScript::OnCreate");
	}
}

void CDefaultScript::OnStart()
{
	if (Engine.Debug)
	{
		Engine.Debug->Log("CDefaultScript::OnStart");
	}
}

void CDefaultScript::OnUpdate()
{
	// Per-frame gameplay logic goes here.
}

void CDefaultScript::OnFixedUpdate()
{
	// Physics-tick gameplay logic goes here.
}

void CDefaultScript::OnDestroy()
{
	if (Engine.Debug)
	{
		Engine.Debug->Log("CDefaultScript::OnDestroy");
	}
}
