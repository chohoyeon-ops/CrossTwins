#pragma once
namespace ReaderUtils {
inline void displayBaseWithRefreshCycle(GfxRenderer&,int&) {}
inline void displayWithRefreshCycle(GfxRenderer&,int&) {}
template<class Render> void renderAntiAliased(GfxRenderer&,Render render) { render(); }
}
