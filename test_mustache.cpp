#include <gtest/gtest.h>
#include <boost/mustache.hpp>
#include <boost/json.hpp>
#include <boost/property_tree/ptree.hpp>

class MustacheTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Setup property tree data
        ptreeData.put("name", "John");
        ptreeData.put("age", 30);
        ptreeData.put("isActive", true);

        // Setup JSON data
        jsonData = boost::json::parse(R"({
            "name": "John",
            "age": 30,
            "isActive": true
        })");
    }

    boost::property_tree::ptree ptreeData;
    boost::json::value jsonData;
};

TEST_F(MustacheTest, BasicVariableInterpolation)
{
    std::string templ = "Hello {{name}}!";

    // Test with PropertyTree
    std::string ptreeResult = boost::mustache::render(templ, ptreeData);
    EXPECT_EQ(ptreeResult, "Hello John!");

    // Test with JSON
    std::string jsonResult = boost::mustache::render(templ, jsonData);
    EXPECT_EQ(jsonResult, "Hello John!");
}

TEST_F(MustacheTest, NumericFormatting)
{
    ptreeData.put("number", 123.456);
    jsonData.as_object()["number"] = 123.456;

    std::string templ = "Value: {{number}}";

    std::string ptreeResult = boost::mustache::render(templ, ptreeData);
    EXPECT_EQ(ptreeResult, "Value: 123.456");

    std::string jsonResult = boost::mustache::render(templ, jsonData);
    EXPECT_EQ(jsonResult, "Value: 123.456");
}

TEST_F(MustacheTest, BooleanSections)
{
    std::string templ = "{{#isActive}}Active{{/isActive}}{{^isActive}}Inactive{{/isActive}}";

    std::string ptreeResult = boost::mustache::render(templ, ptreeData);
    EXPECT_EQ(ptreeResult, "Active");

    std::string jsonResult = boost::mustache::render(templ, jsonData);
    EXPECT_EQ(jsonResult, "Active");
}

TEST_F(MustacheTest, ListIteration)
{
    // Setup array data in PropertyTree
    boost::property_tree::ptree items;
    boost::property_tree::ptree item1, item2;
    item1.put("name", "Item1");
    item2.put("name", "Item2");
    items.push_back(std::make_pair("", item1));
    items.push_back(std::make_pair("", item2));
    ptreeData.add_child("items", items);

    // Setup array data in JSON
    jsonData.as_object()["items"] = boost::json::array{
            {{"name", "Item1"}},
            {{"name", "Item2"}}
    };

    std::string templ = "{{#items}}- {{name}}\n{{/items}}";

    std::string ptreeResult = boost::mustache::render(templ, ptreeData);
    EXPECT_EQ(ptreeResult, "- Item1\n- Item2\n");

    std::string jsonResult = boost::mustache::render(templ, jsonData);
    EXPECT_EQ(jsonResult, "- Item1\n- Item2\n");
}

TEST_F(MustacheTest, NestedSections)
{
    std::string jsonStr = R"({
        "user": {
            "name": "John",
            "details": {
                "age": 30
            }
        }
    })";

    auto nestedJson = boost::json::parse(jsonStr);

    std::string templ = "{{#user}}Name: {{name}}, Age: {{#details}}{{age}}{{/details}}{{/user}}";

    std::string jsonResult = boost::mustache::render(templ, nestedJson);
    EXPECT_EQ(jsonResult, "Name: John, Age: 30");
}

TEST_F(MustacheTest, HtmlEscaping)
{
    ptreeData.put("html", "<p>Hello & World</p>");
    jsonData.as_object()["html"] = "<p>Hello & World</p>";

    std::string templ = "{{html}} vs {{{html}}} vs {{&html}}";

    std::string ptreeResult = boost::mustache::render(templ, ptreeData);
    EXPECT_EQ(ptreeResult, "&lt;p&gt;Hello &amp; World&lt;/p&gt; vs <p>Hello & World</p> vs <p>Hello & World</p>");

    std::string jsonResult = boost::mustache::render(templ, jsonData);
    EXPECT_EQ(jsonResult, "&lt;p&gt;Hello &amp; World&lt;/p&gt; vs <p>Hello & World</p> vs <p>Hello & World</p>");
}


TEST_F(MustacheTest, CustomRendereFunc)
{
    std::string templ = "Hello {{#UPPER}}{{name}}{{/UPPER}}!";

    boost::mustache::registerFunction(
        "UPPER", [](std::string_view text, boost::mustache::Renderer* renderer, boost::mustache::Context* ctx) {
            std::string result = renderer->render(text, ctx);
            std::transform(result.begin(), result.end(), result.begin(), ::toupper);
            return result;
        });

    // Test with PropertyTree
    std::string ptreeResult = boost::mustache::render(templ, ptreeData);
    EXPECT_EQ(ptreeResult, "Hello JOHN!");

    // Test with JSON
    std::string jsonResult = boost::mustache::render(templ, jsonData);
    EXPECT_EQ(jsonResult, "Hello JOHN!");
}