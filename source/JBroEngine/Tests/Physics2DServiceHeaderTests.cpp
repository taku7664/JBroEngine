#include <JBro/Framework2D/ServiceContext.h>

#include <type_traits>

static_assert(std::is_empty_v<JBro::Service::Physics2DService>);
static_assert(std::is_standard_layout_v<JBro::Service::Physics2DService>);
static_assert(std::is_trivially_copyable_v<JBro::Service::Physics2DService>);

// Compile each negative probe separately. Neither internal type may be exposed.
#if defined(JBRO_TEST_PHYSICS_SERVICE_CONTEXT_LEAK)
static_assert(sizeof(JBro::SystemContext) > 0);
#endif

#if defined(JBRO_TEST_PHYSICS_SERVICE_SYSTEM_LEAK)
static_assert(sizeof(JBro::System::IPhysics2DSystem) > 0);
#endif
