#pragma once
#include <boost/property_tree/ptree.hpp>
#include <boost/json.hpp>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm/transform.hpp>
#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <vector>
#include <optional>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace boost::mustache
{
    // Partial resolver interface
    class PartialResolver
    {
    public:
        virtual ~PartialResolver() = default;
        virtual std::string getPartial(std::string_view name) = 0;
    };

    class Renderer;

    // Context base class
    class Context
    {
    public:
        inline explicit Context(std::shared_ptr<PartialResolver> resolver = nullptr)
            : m_partialResolver(std::move(resolver))
        {
        }

        virtual ~Context() = default;
        virtual std::string stringValue(std::string_view key) const = 0;
        virtual bool isFalse(std::string_view key) const = 0;
        virtual size_t listCount(std::string_view key) const = 0;
        virtual void push(std::string_view key, int index = -1) = 0;
        virtual void pop() = 0;

        std::shared_ptr<PartialResolver> partialResolver() const
        {
            return m_partialResolver;
        }

        virtual bool canEval(std::string_view) const
        {
            return false;
        }

        virtual std::string eval(std::string_view, std::string_view, Renderer*)
        {
            return {};
        }

        std::string partialValue(std::string_view key) const
        {
            if (!m_partialResolver) return {};
            return m_partialResolver->getPartial(key);
        }

    private:
        std::shared_ptr<PartialResolver> m_partialResolver;
    };

    // PropertyTree context implementation
    class PropertyTreeContext : public Context
    {
    public:
        using EvalFunction = std::function<std::string(std::string_view, Renderer*, Context*)>;

        explicit PropertyTreeContext(const boost::property_tree::ptree& root,
                                     std::shared_ptr<PartialResolver> resolver = nullptr)
            : Context(std::move(resolver))
        {
            m_contextStack.push_back(root);
        }

        boost::property_tree::ptree getValue(std::string_view key) const
        {
            if (key == ".")
            {
                return m_contextStack.back();
            }

            std::string keyStr{key};

            for (const auto& it : boost::adaptors::reverse(m_contextStack))
            {
                if (auto childIt = it.get_child_optional(keyStr))
                {
                    return *childIt;
                }

                continue;
            }
            return {};
        }

        bool isFalse(std::string_view key) const override
        {
            auto value = getValue(key);
            try
            {
                bool boolValue = value.get_value<bool>();
                return !boolValue;
            }
            catch (const boost::property_tree::ptree_bad_data&)
            {
                try
                {
                    std::string stringValue = value.get_value<std::string>();
                    std::string lowerStringValue = stringValue;
                    boost::range::transform(lowerStringValue, lowerStringValue.begin(), ::tolower);
                    return (lowerStringValue == "false" || stringValue.empty());
                }
                catch (...)
                {
                    return true;
                }
            }
            catch (...)
            {
                return true;
            }
        }

        std::string stringValue(std::string_view key) const override
        {
            auto value = getValue(key);

            if (auto doubleValue = value.get_value_optional<double>(); doubleValue.has_value())
            {
                std::ostringstream oss;
                oss.precision(6); // Default precision like fmt

                const auto dvalue = *doubleValue;

                if (std::floor(dvalue) == dvalue)
                {
                    oss << std::fixed << std::setprecision(0) << dvalue;
                }
                else
                {
                    oss << std::defaultfloat << dvalue;
                }

                return oss.str();
            }

            if (auto floatValue = value.get_value_optional<float>(); floatValue.has_value())
            {
                std::ostringstream oss;
                oss.precision(6);
                oss << std::defaultfloat << *floatValue;
                return oss.str();
            }

            if (auto strValue = value.get_value_optional<std::string>())
            {
                return *strValue;
            }

            return {};
        }

        void push(std::string_view key, int index = -1) override
        {
            auto value = getValue(key);
            if (value.empty())
            {
                m_contextStack.emplace_back();
                return;
            }

            if (index >= 0)
            {
                int currentIndex = 0;
                for (auto& val : value | boost::adaptors::map_values)
                {
                    if (currentIndex == index)
                    {
                        m_contextStack.push_back(val);
                        return;
                    }
                    currentIndex++;
                }
                m_contextStack.emplace_back();
            }
            else
            {
                m_contextStack.push_back(value);
            }
        }

        void pop() override
        {
            if (!m_contextStack.empty())
            {
                m_contextStack.pop_back();
            }
        }

        size_t listCount(std::string_view key) const override
        {
            auto value = getValue(key);
            return value.size();
        }

        bool canEval(std::string_view) const override
        {
            return false;
        }

        std::string eval(std::string_view, std::string_view, Renderer*) override
        {
            return {};
        }

    private:
        std::vector<boost::property_tree::ptree> m_contextStack;
    };


    // File-based partial resolver
    class PartialFileLoader : public PartialResolver
    {
    public:
        explicit PartialFileLoader(std::string_view basePath)
            : m_basePath(basePath)
        {
        }

        std::string getPartial(std::string_view name) override
        {
            std::string nameStr{name};
            if (m_cache.count(nameStr))
            {
                return m_cache[nameStr];
            }

            auto path = std::filesystem::path(m_basePath) / (std::string(name) + ".mustache");
            std::ifstream file(path);
            if (file)
            {
                std::stringstream buffer;
                buffer << file.rdbuf();
                auto content = buffer.str();
                m_cache[nameStr] = content;
                return content;
            }
            return {};
        }

    private:
        std::string m_basePath;
        std::unordered_map<std::string, std::string> m_cache;
    };

    // Tag structure
    struct Tag
    {
        enum class type
        {
            Null,
            Value,
            SectionStart,
            InvertedSectionStart,
            SectionEnd,
            Partial,
            Comment,
            SetDelimiter
        };

        enum class escape_mode
        {
            Escape,
            Unescape,
            Raw
        };

        type type{type::Null};
        std::string key;
        size_t start{0};
        size_t end{0};
        escape_mode escapeMode{escape_mode::Escape};
        size_t indentation{0};
    };


    class Renderer
    {
    public:
        Renderer()
            : m_errorPos(std::nullopt)
              , m_defaultTagStartMarker("{{")
              , m_defaultTagEndMarker("}}")
        {
            m_tagStartMarker = m_defaultTagStartMarker;
            m_tagEndMarker = m_defaultTagEndMarker;
        }

        std::string_view error() const { return m_error; }
        std::optional<size_t> errorPos() const { return m_errorPos; }
        std::string_view errorPartial() const { return m_errorPartial; }

        void setTagMarkers(std::string_view startMarker, std::string_view endMarker)
        {
            m_defaultTagStartMarker = std::string(startMarker);
            m_defaultTagEndMarker = std::string(endMarker);
        }

        std::string render(const std::string_view templ, Context* context)
        {
            m_error.clear();
            m_errorPos = std::nullopt;
            m_errorPartial.clear();
            m_tagStartMarker = m_defaultTagStartMarker;
            m_tagEndMarker = m_defaultTagEndMarker;
            return render(templ, 0, templ.length(), context);
        }

    private:
        static std::string escapeHtml(std::string_view input)
        {
            std::string result;
            result.reserve(input.size());
            for (char c : input)
            {
                switch (c)
                {
                case '&': result += "&amp;";
                    break;
                case '<': result += "&lt;";
                    break;
                case '>': result += "&gt;";
                    break;
                case '"': result += "&quot;";
                    break;
                default: result += c;
                }
            }
            return result;
        }

        static std::string unescapeHtml(std::string_view escaped)
        {
            std::string result{escaped};
            static const std::vector<std::pair<std::string_view, char>> replacements = {
                {"&lt;", '<'},
                {"&gt;", '>'},
                {"&quot;", '"'},
                {"&amp;", '&'}
            };
            for (const auto& [pattern, replacement] : replacements)
            {
                size_t pos = 0;
                while ((pos = result.find(pattern, pos)) != std::string::npos)
                {
                    result.replace(pos, pattern.length(), 1, replacement);
                    pos += 1;
                }
            }
            return result;
        }

        std::string render(std::string_view templ, size_t startPos, size_t endPos, Context* context)
        {
            std::string output;
            size_t lastTagEnd = startPos;

            while (!m_errorPos)
            {
                Tag tag = findTag(templ, lastTagEnd, endPos);
                if (tag.type == Tag::type::Null)
                {
                    output.append(templ.substr(lastTagEnd, endPos - lastTagEnd));
                    break;
                }

                output.append(templ.substr(lastTagEnd, tag.start - lastTagEnd));

                switch (tag.type)
                {
                case Tag::type::Value:
                    {
                        std::string value = context->stringValue(tag.key);
                        if (tag.escapeMode == Tag::escape_mode::Escape)
                        {
                            value = escapeHtml(value);
                        }
                        else if (tag.escapeMode == Tag::escape_mode::Unescape)
                        {
                            value = unescapeHtml(value);
                        }
                        output += value;
                        lastTagEnd = tag.end;
                        break;
                    }

                case Tag::type::SectionStart:
                    {
                        Tag endTag = findEndTag(templ, tag, endPos);
                        if (endTag.type == Tag::type::Null)
                        {
                            if (!m_errorPos)
                            {
                                setError("No matching end tag found for section", tag.start);
                            }
                        }
                        else
                        {
                            size_t listCount = context->listCount(tag.key);
                            if (listCount > 0)
                            {
                                for (size_t i = 0; i < listCount; ++i)
                                {
                                    context->push(tag.key, i);
                                    output += render(templ, tag.end, endTag.start, context);
                                    context->pop();
                                }
                            }
                            else if (context->canEval(tag.key))
                            {
                                output += context->eval(tag.key,
                                                        templ.substr(tag.end, endTag.start - tag.end),
                                                        this);
                            }
                            else if (!context->isFalse(tag.key))
                            {
                                context->push(tag.key);
                                output += render(templ, tag.end, endTag.start, context);
                                context->pop();
                            }
                            lastTagEnd = endTag.end;
                        }
                        break;
                    }
                case Tag::type::InvertedSectionStart:
                    {
                        Tag endTag = findEndTag(templ, tag, endPos);
                        if (endTag.type == Tag::type::Null)
                        {
                            if (!m_errorPos)
                            {
                                setError("No matching end tag found for inverted section", tag.start);
                            }
                        }
                        else
                        {
                            if (context->isFalse(tag.key))
                            {
                                output += render(templ, tag.end, endTag.start, context);
                            }
                            lastTagEnd = endTag.end;
                        }
                        break;
                    }

                case Tag::type::Partial:
                    {
                        std::string tagStartMarker = m_tagStartMarker;
                        std::string tagEndMarker = m_tagEndMarker;
                        m_tagStartMarker = m_defaultTagStartMarker;
                        m_tagEndMarker = m_defaultTagEndMarker;
                        m_partialStack.push_back(tag.key);

                        std::string partialContent = context->partialValue(tag.key);
                        if (tag.indentation > 0)
                        {
                            output += std::string(tag.indentation, ' ');
                            size_t pos = 0;
                            while ((pos = partialContent.find('\n', pos)) != std::string::npos)
                            {
                                if (pos < partialContent.length() - 1)
                                {
                                    partialContent.insert(pos + 1, std::string(tag.indentation, ' '));
                                }
                                pos += tag.indentation + 1;
                            }
                        }

                        output += render(partialContent, 0, partialContent.length(), context);
                        lastTagEnd = tag.end;
                        m_partialStack.pop_back();
                        m_tagStartMarker = tagStartMarker;
                        m_tagEndMarker = tagEndMarker;
                        break;
                    }

                case Tag::type::SetDelimiter:
                    lastTagEnd = tag.end;
                    break;

                case Tag::type::Comment:
                    lastTagEnd = tag.end;
                    break;

                case Tag::type::SectionEnd:
                    setError("Unexpected end tag", tag.start);
                    lastTagEnd = tag.end;
                    break;

                case Tag::type::Null:
                    break;
                }
            }
            return output;
        }

        Tag findTag(std::string_view content, size_t pos, size_t endPos)
        {
            size_t tagStartPos = content.find(m_tagStartMarker, pos);
            if (tagStartPos == std::string::npos || tagStartPos >= endPos)
            {
                return Tag{};
            }

            size_t tagEndPos = content.find(m_tagEndMarker, tagStartPos + m_tagStartMarker.length());
            if (tagEndPos == std::string::npos)
            {
                return Tag{};
            }

            tagEndPos += m_tagEndMarker.length();

            Tag tag;
            tag.start = tagStartPos;
            tag.end = tagEndPos;

            pos = tagStartPos + m_tagStartMarker.length();
            endPos = tagEndPos - m_tagEndMarker.length();

            char typeChar = content[pos];
            if (typeChar == '#')
            {
                tag.type = Tag::type::SectionStart;
                tag.key = std::string(readTagName(content, pos + 1, endPos));
            }
            else if (typeChar == '^')
            {
                tag.type = Tag::type::InvertedSectionStart;
                tag.key = std::string(readTagName(content, pos + 1, endPos));
            }
            else if (typeChar == '/')
            {
                tag.type = Tag::type::SectionEnd;
                tag.key = std::string(readTagName(content, pos + 1, endPos));
            }
            else if (typeChar == '!')
            {
                tag.type = Tag::type::Comment;
            }
            else if (typeChar == '>')
            {
                tag.type = Tag::type::Partial;
                tag.key = std::string(readTagName(content, pos + 1, endPos));
            }
            else if (typeChar == '=')
            {
                tag.type = Tag::type::SetDelimiter;
                readSetDelimiter(content, pos + 1, tagEndPos - m_tagEndMarker.length());
            }
            else
            {
                if (typeChar == '&')
                {
                    tag.escapeMode = Tag::escape_mode::Unescape;
                    ++pos;
                }
                else if (typeChar == '{')
                {
                    tag.escapeMode = Tag::escape_mode::Raw;
                    ++pos;
                    const size_t endTache = content.find('}', pos);
                    if (endTache == tag.end - m_tagEndMarker.length())
                    {
                        ++tag.end;
                    }
                    else
                    {
                        endPos = endTache;
                    }
                }
                tag.type = Tag::type::Value;
                tag.key = std::string(readTagName(content, pos, endPos));
            }

            if (tag.type != Tag::type::Value)
            {
                expandTag(tag, content);
            }

            return tag;
        }

        Tag findEndTag(std::string_view content, const Tag& startTag, size_t endPos)
        {
            int tagDepth = 1;
            size_t pos = startTag.end;

            while (true)
            {
                Tag nextTag = findTag(content, pos, endPos);
                if (nextTag.type == Tag::type::Null)
                {
                    return nextTag;
                }
                else if (nextTag.type == Tag::type::SectionStart ||
                    nextTag.type == Tag::type::InvertedSectionStart)
                {
                    ++tagDepth;
                }
                else if (nextTag.type == Tag::type::SectionEnd)
                {
                    --tagDepth;
                    if (tagDepth == 0)
                    {
                        if (nextTag.key != startTag.key)
                        {
                            setError("Tag start/end key mismatch", nextTag.start);
                            return Tag{};
                        }
                        return nextTag;
                    }
                }
                pos = nextTag.end;
            }
        }

        void setError(std::string_view error, size_t pos)
        {
            m_error = std::string(error);
            m_errorPos = pos;
            if (!m_partialStack.empty())
            {
                m_errorPartial = m_partialStack.back();
            }
        }

        void readSetDelimiter(std::string_view content, size_t pos, size_t endPos)
        {
            std::string startMarker;
            std::string endMarker;

            while (pos < endPos && std::isspace(content[pos]))
            {
                ++pos;
            }

            while (pos < endPos && !std::isspace(content[pos]))
            {
                if (content[pos] == '=')
                {
                    setError("Custom delimiters may not contain '='", pos);
                    return;
                }
                startMarker += content[pos++];
            }

            while (pos < endPos && std::isspace(content[pos]))
            {
                ++pos;
            }

            while (pos < endPos - 1 && !std::isspace(content[pos]))
            {
                if (content[pos] == '=')
                {
                    setError("Custom delimiters may not contain '='", pos);
                    return;
                }
                endMarker += content[pos++];
            }

            m_tagStartMarker = std::move(startMarker);
            m_tagEndMarker = std::move(endMarker);
        }

        static std::string_view readTagName(std::string_view content, size_t pos, size_t endPos)
        {
            while (pos < endPos && std::isspace(content[pos]))
            {
                ++pos;
            }

            size_t start = pos;

            while (pos < endPos && !std::isspace(content[pos]))
            {
                ++pos;
            }

            return content.substr(start, pos - start);
        }

        static void expandTag(Tag& tag, std::string_view content)
        {
            size_t start = tag.start;
            size_t end = tag.end;
            size_t indentation = 0;

            while (start > 0 && content[start - 1] != '\n')
            {
                --start;
                if (!std::isspace(content[start]))
                {
                    return;
                }
                else if (std::isspace(content[start]) && content[start] != '\n')
                {
                    ++indentation;
                }
            }

            while (end < content.length() && content[end - 1] != '\n')
            {
                if (end < content.length() && !std::isspace(content[end]))
                {
                    return;
                }
                ++end;
            }

            tag.start = start;
            tag.end = end;
            tag.indentation = indentation;
        }

    private:
        std::vector<std::string> m_partialStack;
        std::string m_error;
        std::optional<size_t> m_errorPos;
        std::string m_errorPartial;
        std::string m_tagStartMarker;
        std::string m_tagEndMarker;
        std::string m_defaultTagStartMarker;
        std::string m_defaultTagEndMarker;
    };

    // Add new JsonContext class
    class JsonContext : public Context
    {
    public:
        explicit JsonContext(const boost::json::value& root,
            std::shared_ptr<PartialResolver> resolver = nullptr)
            : Context(std::move(resolver))
        {
            m_contextStack.push_back(root);
        }

        boost::json::value getValue(std::string_view key) const
        {
            if (key == ".") {
                return m_contextStack.back();
            }

            std::string keyStr{ key };

            for (const auto& ctx : boost::adaptors::reverse(m_contextStack))
            {
                if (ctx.is_object()) {
                    const auto& obj = ctx.as_object();
                    if (auto it = obj.find(keyStr); it != obj.end()) {
                        return it->value();
                    }
                }
            }
            return {};
        }

        bool isFalse(std::string_view key) const override
        {
            auto value = getValue(key);

            if (value.is_bool()) {
                return !value.as_bool();
            }

            if (value.is_string()) {
                std::string str = value.as_string().c_str();
                std::string lowerStr = str;
                boost::range::transform(lowerStr, lowerStr.begin(), ::tolower);
                return lowerStr == "false" || str.empty();
            }

            if (value.is_null()) {
                return true;
            }

            return false;
        }

        std::string stringValue(std::string_view key) const override
        {
            auto value = getValue(key);

            if (value.is_double()) {
                std::ostringstream oss;
                oss.precision(6);

                if (const auto dvalue = value.as_double(); std::floor(dvalue) == dvalue) {
                    oss << std::fixed << std::setprecision(0) << dvalue;
                }
                else {
                    oss << std::defaultfloat << dvalue;
                }
                return oss.str();
            }

            if (value.is_string()) {
                return std::string(value.as_string());
            }

            if (value.is_bool()) {
                return value.as_bool() ? "true" : "false";
            }

            if (value.is_number()) {
                return std::to_string(value.as_int64());
            }

            return {};
        }

        size_t listCount(std::string_view key) const override
        {
            auto value = getValue(key);
            return value.is_array() ? value.as_array().size() : 0;
        }

        void push(std::string_view key, int index = -1) override
        {
            auto value = getValue(key);

            if (value.is_null()) {
                m_contextStack.emplace_back();
                return;
            }

            if (index >= 0 && value.is_array()) {
                const auto& arr = value.as_array();
                if (static_cast<size_t>(index) < arr.size()) {
                    m_contextStack.push_back(arr[index]);
                }
                else {
                    m_contextStack.emplace_back();
                }
            }
            else {
                m_contextStack.push_back(value);
            }
        }

        void pop() override
        {
            if (!m_contextStack.empty()) {
                m_contextStack.pop_back();
            }
        }

    private:
        std::vector<boost::json::value> m_contextStack;
    };


    inline std::string render(std::string_view templateString, const boost::property_tree::ptree& args)
    {
        PropertyTreeContext context(args);
        Renderer renderer;
        return renderer.render(templateString, &context);
    }

    inline std::string render(std::string_view templateString, const boost::json::value& args)
    {
        JsonContext context(args);
        Renderer renderer;
        return renderer.render(templateString, &context);
    }
}
