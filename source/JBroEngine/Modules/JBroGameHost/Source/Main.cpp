#if defined(JBRO_GAME_DIMENSION_2D)
#include <JBro/Framework2D/Framework2D.h>
#elif defined(JBRO_GAME_DIMENSION_3D)
#include <JBro/Framework3D/Framework3D.h>
#else
#error A game dimension must be selected.
#endif

int main()
{
#if defined(JBRO_GAME_DIMENSION_2D)
    JBro::IFramework* framework = JBro::CreateFramework2D();
    JBro::DestroyFramework2D(framework);
#else
    JBro::IFramework* framework = JBro::CreateFramework3D();
    JBro::DestroyFramework3D(framework);
#endif

    return 0;
}
