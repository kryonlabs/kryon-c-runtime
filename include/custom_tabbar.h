#ifndef CUSTOM_TABBAR_H
#define CUSTOM_TABBAR_H

#include "custom_components.h"

// Register the TabBar component handler
void register_tabbar_component(void);

// The actual TabBar handler (internal)
bool handle_tabbar_component(RenderContext* ctx, RenderElement* element, FILE* debug_file);

#endif // CUSTOM_TABBAR_H
