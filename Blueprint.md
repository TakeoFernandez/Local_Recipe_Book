# Project Blueprint: C++ In-Memory Search Engine compiled to WebAssembly (WASM)

This blueprint outlines how to build a high-performance, zero-dependency, local recipe search engine. Instead of a server-side database, this design compiles your custom **C++ Standard Library (`std`)** data structures and **Inverted Index** directly into a **WebAssembly (WASM)** binary. 

This enables a gorgeous web frontend that runs your native C++ search algorithms directly inside the recruiter's web browser with **zero backend servers**.

---

## 1. Architectural Overview

The user loads the webpage (which can be hosted 100% free on GitHub Pages). On startup, the browser loads the compiled C++ binary (`recipe_engine.wasm`) and feeds the raw recipe dataset (CSV/JSON) directly into the C++ memory space to construct your **Inverted Index**.

```
  [ Browser Client / HTML ] <───> [ JS Event Handlers ]
                                           │
                                           ▼ (Calls exported C++ functions)
┌────────────────────────────────────────────────────────────────────────┐
│                      WebAssembly Sandbox (C++)                         │
│                                                                        │
│  [ Custom Inverted Index ] <──────────────> [ C++ Match Engine ]       │
│  std::unordered_map<string, vector*>         - Fast intersection match │
│                                              - Smart Pointer Storage   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. WebAssembly-Compatible C++ Structures

To allow JavaScript to call our C++ code seamlessly, we use Emscripten's **Embind** library. This maps C++ classes and structures directly to JavaScript objects.

```cpp
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <emscripten/bind.h>

using namespace emscripten;

// Struct representing recipe ingredients
struct RecipeIngredient {
    std::string name;
    double amount;
    std::string unit;
};

// Core Recipe Object
struct Recipe {
    int id;
    std::string title;
    std::string instructions;
    int ready_in_minutes;
    int servings;
    std::vector<RecipeIngredient> ingredients;
};

// Simple output structure to return results to Javascript
struct MatchResult {
    int recipe_id;
    std::string title;
    int matching_ingredients_count;
    int missing_ingredients_count;
};

// Database class managing memory, indexation, and lookups
class RecipeDatabase {
private:
    std::vector<std::unique_ptr<Recipe>> recipes_storage;
    std::unordered_map<std::string, std::vector<const Recipe*>> ingredient_to_recipes;

public:
    RecipeDatabase() = default;

    // Javascript will call this to populate the C++ memory
    void add_recipe(int id, std::string title, std::string instructions, int ready_time, int servings, std::string ingredients_csv) {
        auto recipe = std::make_unique<Recipe>();
        recipe->id = id;
        recipe->title = title;
        recipe->instructions = instructions;
        recipe->ready_in_minutes = ready_time;
        recipe->servings = servings;

        // Tokenize ingredient string (e.g. "salt,pepper,flour")
        std::stringstream ss(ingredients_csv);
        std::string ing_name;
        while (std::getline(ss, ing_name, ',')) {
            if (!ing_name.empty()) {
                recipe->ingredients.push_back({ing_name, 1.0, ""});
                
                std::string normalized = normalize_string(ing_name);
                ingredient_to_recipes[normalized].push_back(recipe.get());
            }
        }
        recipes_storage.push_back(std::move(recipe));
    }

    // Main search engine triggered on button click
    std::vector<MatchResult> search_pantry(std::string pantry_csv) const {
        std::vector<std::string> pantry;
        std::stringstream ss(pantry_csv);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                pantry.push_back(normalize_string(item));
            }
        }

        std::unordered_map<const Recipe*, int> recipe_match_counts;
        for (const auto& ingredient : pantry) {
            auto it = ingredient_to_recipes.find(ingredient);
            if (it != ingredient_to_recipes.end()) {
                for (const Recipe* recipe : it->second) {
                    recipe_match_counts[recipe]++;
                }
            }
        }

        std::vector<MatchResult> results;
        for (const auto& [recipe, match_count] : recipe_match_counts) {
            results.push_back({
                recipe->id,
                recipe->title,
                match_count,
                static_cast<int>(recipe->ingredients.size() - match_count)
            });
        }

        // Sort: fewest missing ingredients first
        std::sort(results.begin(), results.end(), [](const MatchResult& a, const MatchResult& b) {
            if (a.missing_ingredients_count != b.missing_ingredients_count) {
                return a.missing_ingredients_count < b.missing_ingredients_count;
            }
            return a.matching_ingredients_count > b.matching_ingredients_count;
        });

        return results;
    }

private:
    static std::string normalize_string(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        // Trim whitespace
        str.erase(0, str.find_first_not_of(" \t"));
        str.erase(str.find_last_not_of(" \t") + 1);
        return str;
    }
};

// --- EMSCRIPTEN BINDINGS ---
// This exposes C++ structures and classes directly into Javascript
EMSCRIPTEN_BINDINGS(recipe_engine) {
    value_object<MatchResult>("MatchResult")
        .field("recipe_id", &MatchResult::recipe_id)
        .field("title", &MatchResult::title)
        .field("matching_ingredients_count", &MatchResult::matching_ingredients_count)
        .field("missing_ingredients_count", &MatchResult::missing_ingredients_count);

    register_vector<MatchResult>("VectorMatchResult");

    class_<RecipeDatabase>("RecipeDatabase")
        .constructor<>()
        .function("add_recipe", &RecipeDatabase::add_recipe)
        .function("search_pantry", &RecipeDatabase::search_pantry);
}
```

---

## 3. How to Compile (using Emscripten)

To compile your C++ code into WebAssembly, install the Emscripten SDK (emsdk) and run the `emcc` compiler:

```bash
# Install Emscripten SDK (do this once)
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# Compile C++ to WASM with Embind enabled
emcc -O3 --bind -s WASM=1 -o recipe_engine.js recipe_engine.cpp
```

This compiles your code into:
1. `recipe_engine.wasm` (Compiled high-performance binary module)
2. `recipe_engine.js` (JavaScript glue-code linking the webpage to your WASM binary)

---

## 4. Frontend Integration (HTML/JS)

Once compiled, you can load your database in pure frontend Javascript.

### Step A: Load the C++ WebAssembly Module
```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <title>BiteCraft WASM // Premium Offline Engine</title>
  <!-- Load the Emscripten glue-code -->
  <script src="recipe_engine.js"></script>
</head>
<body>
  ...
  <script>
    let db;

    // Wait for the WebAssembly module to load and initialize
    Module.onRuntimeInitialized = async () => {
        console.log("C++ WebAssembly Engine Ready!");
        // Instantiate our C++ class directly in JS
        db = new Module.RecipeDatabase();
        
        // Load local recipes.csv and stream them to our C++ Database memory space
        await loadAndPopulateRecipes();
    };
  </script>
</body>
</html>
```

### Step B: Load the Dataset and Stream to C++ Memory
```javascript
async function loadAndPopulateRecipes() {
    const response = await fetch('recipes.csv');
    const text = await response.text();
    const rows = text.split('\n').slice(1); // skip headers

    for (const row of rows) {
        if (!row.trim()) continue;
        
        // Parse simple columns (e.g. ID, Title, Instructions, Prep, Servings, Ingredients)
        const [id, title, instructions, prep, servings, ingredients] = parseCSVRow(row);
        
        // Push raw data into the high-performance C++ storage space
        db.add_recipe(
            parseInt(id, 10),
            title,
            instructions,
            parseInt(prep, 10),
            parseInt(servings, 10),
            ingredients // Comma separated list of ingredients
        );
    }
    console.log("C++ Inverted Index fully populated in memory!");
}
```

### Step C: Execute High-Speed C++ Searches
```javascript
function onSearchButtonClick() {
    const pantryInput = "chicken,garlic,onions,olive oil";
    
    // Call the compiled C++ search function!
    // It runs the inverted index and Jaccard sorting in C++ and returns a C++ vector.
    const cppResultsVector = db.search_pantry(pantryInput);
    
    // Convert the returned custom C++ vector into a standard JS array
    const results = [];
    for (let i = 0; i < cppResultsVector.size(); i++) {
        results.push(cppResultsVector.get(i));
    }
    
    // Render the ranked recipes to the HTML interface
    displayRecipes(results);
}
```

---

## 5. C++ / WASM Resume Talking Points

This architecture completely modernizes your resume. It showcases that you can solve high-performance systems engineering problems and compile those solutions for modern cloud-native/browser-native client environments:

> * **WebAssembly (WASM) Cross-Compilation**: Ported native standard library C++ search utilities to the web platform by building a compiled WebAssembly binary using the Emscripten SDK, delivering close-to-native code execution speeds directly inside client sandboxes.
> * **High-Speed In-Memory Inverted Index**: Engineered an in-memory database layout mapping ingredients to pointer vectors, accelerating recipe lookup and sorting operations to constant time ($O(1)$ lookup complexity per key).
> * **Interface Boundaries & Interoperability**: Implemented robust interface boundaries between JavaScript and WebAssembly using **Embind**, enabling seamless cross-language object mapping, memory sharing, and raw data streaming.
> * **Automated Memory Safety**: Handled runtime lifecycle allocations using C++ smart pointer vectors (`std::unique_ptr` and memory moving semantics), guaranteeing leak-free processing of massive datasets in a sandboxed browser workspace.
> * **Zero-Server Static Deployment**: Architected a completely serverless offline application design, enabling free host deployments (e.g. GitHub Pages) while maintaining secure, lightning-fast client-side execution.
