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

    static std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string normalize_string(std::string str) {
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return trim(str);
    }

    // Check singular/plural variations and simple stem equivalence
    static bool stem_equal(const std::string& a, const std::string& b) {
        if (a == b) return true;
        if (a + "s" == b || a + "es" == b) return true;
        if (b + "s" == a || b + "es" == a) return true;
        if (a.size() > 1 && a.back() == 'y' && a.substr(0, a.size() - 1) + "ies" == b) return true;
        if (b.size() > 1 && b.back() == 'y' && b.substr(0, b.size() - 1) + "ies" == a) return true;
        return false;
    }

    // Split string into individual whitespace-delimited tokens
    static std::vector<std::string> split_words(const std::string& text) {
        std::vector<std::string> words;
        std::stringstream ss(text);
        std::string word;
        while (ss >> word) {
            words.push_back(word);
        }
        return words;
    }

    // Evaluate whether a user pantry ingredient matches a recipe ingredient
    static bool ingredient_matches(const std::string& pantry_item, const std::string& recipe_ing) {
        std::string p = normalize_string(pantry_item);
        std::string r = normalize_string(recipe_ing);
        if (p.empty() || r.empty()) return false;

        // 1. Exact or singular/plural equality
        if (stem_equal(p, r)) return true;

        // 2. Tokenized word boundary matching
        auto r_words = split_words(r);
        auto p_words = split_words(p);

        // Single-word query term matches any word component in recipe ingredient
        if (p_words.size() == 1) {
            for (const auto& rw : r_words) {
                if (stem_equal(p_words[0], rw)) return true;
            }
        }

        // Substring containment for multi-word or compound ingredients
        if (r.find(p) != std::string::npos || p.find(r) != std::string::npos) {
            return true;
        }

        // Check if all words of pantry term match recipe words
        bool all_p_found = true;
        for (const auto& pw : p_words) {
            bool found = false;
            for (const auto& rw : r_words) {
                if (stem_equal(pw, rw)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                all_p_found = false;
                break;
            }
        }
        if (all_p_found && !p_words.empty()) return true;

        return false;
    }

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
            ing_name = normalize_string(ing_name);
            if (!ing_name.empty()) {
                recipe->ingredients.push_back({ing_name, 1.0, ""});
                ingredient_to_recipes[ing_name].push_back(recipe.get());
            }
        }
        recipes_storage.push_back(std::move(recipe));
    }

    // Main search engine triggered on button click or input update
    std::vector<MatchResult> search_pantry(std::string pantry_csv) const {
        std::vector<std::string> pantry;
        std::stringstream ss(pantry_csv);
        std::string item;
        while (std::getline(ss, item, ',')) {
            item = normalize_string(item);
            if (!item.empty()) {
                // Prevent duplicate pantry entries
                if (std::find(pantry.begin(), pantry.end(), item) == pantry.end()) {
                    pantry.push_back(item);
                }
            }
        }

        if (pantry.empty()) {
            return {};
        }

        std::vector<MatchResult> results;

        for (const auto& recipe : recipes_storage) {
            int matching_count = 0;

            // Count how many recipe ingredients are satisfied by the user's pantry
            for (const auto& ing : recipe->ingredients) {
                std::string norm_ing = normalize_string(ing.name);
                for (const auto& p : pantry) {
                    if (ingredient_matches(p, norm_ing)) {
                        matching_count++;
                        break; // Each recipe ingredient is counted at most once
                    }
                }
            }

            if (matching_count > 0) {
                int missing_count = static_cast<int>(recipe->ingredients.size()) - matching_count;
                if (missing_count < 0) missing_count = 0;

                results.push_back({
                    recipe->id,
                    recipe->title,
                    matching_count,
                    missing_count
                });
            }
        }

        // Sorting:
        // 1. Most matching ingredients first (prioritizes recipes utilizing user's ingredients)
        // 2. Fewest missing ingredients as tie-breaker (easiest to prepare)
        // 3. Alphabetical title
        std::sort(results.begin(), results.end(), [](const MatchResult& a, const MatchResult& b) {
            if (a.matching_ingredients_count != b.matching_ingredients_count) {
                return a.matching_ingredients_count > b.matching_ingredients_count;
            }
            if (a.missing_ingredients_count != b.missing_ingredients_count) {
                return a.missing_ingredients_count < b.missing_ingredients_count;
            }
            return a.title < b.title;
        });

        return results;
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
