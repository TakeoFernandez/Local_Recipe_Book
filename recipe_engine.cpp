#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <iostream>
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

        // Tokenize ingredient string (comma-separated from JS)
        std::stringstream ss(ingredients_csv);
        std::string ing_name;
        while (std::getline(ss, ing_name, ',')) {
            // Trim whitespace
            size_t first = ing_name.find_first_not_of(" \t");
            if (first != std::string::npos) {
                size_t last = ing_name.find_last_not_of(" \t");
                ing_name = ing_name.substr(first, (last - first + 1));
            }
            
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
            // Trim whitespace
            size_t first = item.find_first_not_of(" \t");
            if (first != std::string::npos) {
                size_t last = item.find_last_not_of(" \t");
                item = item.substr(first, (last - first + 1));
            }
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
