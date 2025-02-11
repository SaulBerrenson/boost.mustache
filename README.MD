# Boost.Mustache

A header-only C++ implementation of Mustache templates with Boost.PropertyTree and Boost.JSON support.

## Features

- Header-only library
- Support for both Boost.PropertyTree and Boost.JSON data sources
- Full Mustache specification support
- HTML escaping
- Nested sections
- Array iterations
- Boolean conditionals

## Requirements

- C++17 or later
- Boost (property_tree, json)
- CMake 3.12 or later (for building)

## Installation

- Just copy single header
- Build and install

```bash
cmake -B build
cmake --build build
cmake --install build
```

## Usage

### Basic Variable Interpolation

```c++
#include <boost/mustache.hpp>
#include <boost/json.hpp>
#include <boost/property_tree/ptree.hpp>

// Using Boost.PropertyTree
boost::property_tree::ptree data;
data.put("name", "John");
std::string result = boost::mustache::render("Hello {{name}}!", data);
// Output: "Hello John!"

// Using Boost.JSON
auto json = boost::json::parse(R"({"name": "John"})");
result = boost::mustache::render("Hello {{name}}!", json);
// Output: "Hello John!"
```

### Numeric Formatting

```c++
boost::property_tree::ptree data;
data.put("number", 123.456);
std::string result = boost::mustache::render("Value: {{number}}", data);
// Output: "Value: 123.456"

```

### Boolean Sections

```c++

auto json = boost::json::parse(R"({
    "isActive": true
})");
std::string result = boost::mustache::render(
    "{{#isActive}}Active{{/isActive}}{{^isActive}}Inactive{{/isActive}}", 
    json
);
// Output: "Active"

```

### List Iteration

```cpp
auto json = boost::json::parse(R"({
    "items": [
        {"name": "Item1"},
        {"name": "Item2"}
    ]
})");
std::string result = boost::mustache::render("{{#items}}- {{name}}\n{{/items}}", json);
// Output:
// - Item1
// - Item2

```

### Nested Objects

```cpp
auto json = boost::json::parse(R"({
    "user": {
        "name": "John",
        "details": {
            "age": 30
        }
    }
})");
std::string result = boost::mustache::render(
    "{{#user}}Name: {{name}}, Age: {{#details}}{{age}}{{/details}}{{/user}}", 
    json
);
// Output: "Name: John, Age: 30"

```


### HTML Escaping

```cpp
boost::property_tree::ptree data;
data.put("html", "<p>Hello & World</p>");
std::string result = boost::mustache::render("{{html}} vs {{{html}}} vs {{&html}}", data);
// Output: "&lt;p&gt;Hello &amp; World&lt;/p&gt; vs <p>Hello & World</p> vs <p>Hello & World</p>"

```