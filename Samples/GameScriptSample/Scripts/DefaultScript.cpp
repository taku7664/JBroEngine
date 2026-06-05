#include "pch.h"
#include "DefaultScript.h"

void CDefaultScript::OnCreate()
{
	if (Script.Debug)
	{
		Script.Debug->Log("CDefaultScript::OnCreate");
	}
}

void CDefaultScript::OnStart()
{
	if (Script.Debug)
	{
		Script.Debug->Log("CDefaultScript::OnStart");
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
	if (Script.Debug)
	{
		Script.Debug->Log("CDefaultScript::OnDestroy");
	}
}
