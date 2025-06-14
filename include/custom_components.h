#ifndef CUSTOM_COMPONENTS_H
#define CUSTOM_COMPONENTS_H

#include "renderer.h"

// Custom component handler function type
typedef bool (*CustomComponentHandler)(RenderContext* ctx, RenderElement* element, FILE* debug_file);

// Custom component handler registration
typedef struct {
    const char* component_name;
    CustomComponentHandler handler;
} CustomComponentRegistration;

// Maximum number of custom components
#define MAX_CUSTOM_COMPONENTS 32

// Function declarations
bool register_custom_component(const char* name, CustomComponentHandler handler);
bool process_custom_components(RenderContext* ctx, FILE* debug_file);
const char* get_custom_property_value(RenderElement* element, const char* prop_name, KrbDocument* doc);
void init_custom_components(void);

// Utility functions for custom components
float max_f(float a, float b);

#endif // CUSTOM_COMPONENTS_H