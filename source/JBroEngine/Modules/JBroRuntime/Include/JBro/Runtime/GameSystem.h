#pragma once

namespace JBro::Engine
{
    class RenderWorld2D;
    class CWorld;

    class GameSystem
    {
    public:
        virtual ~GameSystem() = default;

        void Initialize(CWorld& world);
        void FixedUpdate(CWorld& world, float fixedDeltaTime);
        void Update(CWorld& world, float deltaTime);
        void ExtractRender(CWorld& world, RenderWorld2D& renderWorld);
        void Shutdown(CWorld& world);

        bool IsInitialized() const;
        bool IsEnabled() const;
        void SetEnabled(bool enabled);
        virtual int GetExecutionOrder() const;

    protected:
        virtual void OnInitialize(CWorld& world);
        virtual void OnFixedUpdate(CWorld& world, float fixedDeltaTime);
        virtual void OnUpdate(CWorld& world, float deltaTime);
        virtual void OnExtractRender(CWorld& world, RenderWorld2D& renderWorld);
        virtual void OnShutdown(CWorld& world);

    private:
        bool mInitialized = false;
        bool mEnabled = true;
    };
}
