#ifndef KRB_RENDERER_H
#define KRB_RENDERER_H

#include <stdio.h>
#include <stdbool.h>
#include "raylib.h"
#include "krb.h"

#ifndef MAX_ELEMENTS
#define MAX_ELEMENTS 256
#endif

#define MAX_LINE_LENGTH 512
#define INVALID_RESOURCE_INDEX 0xFF

// --- Component Instance Tracking ---
typedef struct ComponentInstance {
    uint8_t definition_index;           // Index into KrbDocument's component_defs array
    struct RenderElement* placeholder;  // Original placeholder element
    struct RenderElement* root;         // Root of instantiated component tree
    struct ComponentInstance* next;     // For linked list of instances
} ComponentInstance;

// --- Render Element Structure ---
typedef struct RenderElement {
    KrbElementHeader header;
    char* text;
    Color bg_color;
    Color fg_color;
    Color border_color;
    uint8_t border_widths[4];
    uint8_t text_alignment;
    struct RenderElement* parent;
    struct RenderElement* children[MAX_ELEMENTS];
    int child_count;
    float font_size;

    // Runtime rendering data
    int render_x;
    int render_y;
    int render_w;
    int render_h;
    bool is_interactive;
    bool is_visible;
    
    int original_index;

    // Resource handling
    uint8_t resource_index;
    Texture2D texture;
    bool texture_loaded;

    // Component instance tracking
    bool is_component_instance;
    bool is_placeholder;
    ComponentInstance* component_instance;
    
    // Custom properties support
    KrbCustomProperty* custom_properties;
    uint8_t custom_prop_count;
    
    // NEW: State properties support
    KrbStatePropertySet* state_properties;
    uint8_t state_prop_count;
    uint8_t current_state;               // Current interaction state flags
    
    // NEW: Cursor support
    uint8_t cursor_type;                 // Cursor type for this element
    
} RenderElement;

// --- Render Context Structure ---
typedef struct RenderContext {
    KrbDocument* doc;                   // Original KRB document
    RenderElement* elements;            // Array of all render elements (including instantiated ones)
    int element_count;                  // Total number of elements (original + instantiated)
    int original_element_count;         // Number of original elements from KRB
    ComponentInstance* instances;       // Linked list of component instances
    
    // Rendering state
    Color default_bg;
    Color default_fg;
    Color default_border;
    int window_width;
    int window_height;
    float scale_factor;
    char* window_title;
    bool resizable;

    RenderElement* roots[MAX_ELEMENTS];
    int root_count;
    
    // NEW: Script support (basic - full implementation would require script engines)
    bool scripts_enabled;
    void* script_context;               // Placeholder for script engine context
} RenderContext;

// --- Context Management Functions ---
RenderContext* create_render_context(KrbDocument* doc, FILE* debug_file);
void free_render_context(RenderContext* ctx);

// --- Element Initialization and Setup Functions ---
void initialize_render_element(RenderElement* el, KrbElementHeader* header, int index, RenderContext* ctx);
void process_app_element_properties(RenderElement* app_element, KrbDocument* doc, RenderContext* ctx, FILE* debug_file);
void apply_element_styling(RenderElement* el, KrbDocument* doc, RenderContext* ctx, FILE* debug_file);
void build_element_tree(RenderContext* ctx, FILE* debug_file);
void find_root_elements(RenderContext* ctx, FILE* debug_file);

// --- Property and Styling Functions ---
void apply_property_to_element(RenderElement* element, KrbProperty* prop, KrbDocument* doc, FILE* debug_file);
void apply_property_inheritance(RenderContext* ctx, FILE* debug_file);
void inherit_properties_recursive(RenderElement* el, RenderContext* ctx, FILE* debug_file);

// NEW: State-based property resolution functions
void update_element_state(RenderElement* el, uint8_t new_state, RenderContext* ctx, FILE* debug_file);
void resolve_state_properties(RenderElement* el, RenderContext* ctx, FILE* debug_file);
Color resolve_state_color_property(RenderElement* el, uint8_t base_prop_id, Color base_color);

// --- Component Expansion Functions ---
bool expand_all_components(RenderContext* ctx, FILE* debug_file);
bool expand_component_for_element(RenderContext* ctx, RenderElement* element, uint8_t component_name_index, FILE* debug_file);
bool find_component_name_property(KrbCustomProperty* custom_props, uint8_t custom_prop_count, 
                                 char** strings, uint8_t* out_component_index);

// --- Layout and Sizing Functions ---
void calculate_element_minimum_size(RenderElement* el, float scale_factor);

// --- Resource and Texture Functions ---
void load_all_textures(RenderContext* ctx, const char* base_dir, FILE* debug_file);

// --- Window and Event Handling Functions ---
void handle_window_resize(RenderContext* ctx);
void handle_mouse_events(RenderContext* ctx, FILE* debug_file);

// NEW: Script-related functions (basic stubs)
bool load_scripts(RenderContext* ctx, FILE* debug_file);
bool execute_script_function(RenderContext* ctx, const char* function_name, FILE* debug_file);

// --- Main Rendering Function ---
void render_element(RenderElement* el, int parent_content_x, int parent_content_y, 
                   int parent_content_width, int parent_content_height, 
                   float scale_factor, FILE* debug_file);

#endif // KRB_RENDERER_H