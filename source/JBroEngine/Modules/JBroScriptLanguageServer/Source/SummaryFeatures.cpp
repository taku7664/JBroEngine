#include "Session.h"

#include <JBro/ScriptCompiler/Summary.h>

namespace JBro::ScriptLanguageServer::Detail
{
    using namespace ScriptCompiler;

    namespace
    {
        Json SummaryJson(std::string_view source, const SummaryEntry& entry)
        {
            Json attributes = Json::array();
            for (const SummaryAttribute& attribute : entry.Attributes)
            {
                Json arguments = Json::array();
                for (const String& argument : attribute.Arguments)
                {
                    arguments.push_back(argument.Std());
                }
                attributes.push_back(Json{{"name", attribute.Name.Std()}, {"arguments", std::move(arguments)}});
            }
            Json members = Json::array();
            for (const SummaryEntry& member : entry.Members)
            {
                members.push_back(SummaryJson(source, member));
            }
            const char* kind = entry.Kind == SummaryEntryKind::Type ? "type"
                : entry.Kind == SummaryEntryKind::Field ? "field" : "function";
            return Json{{"kind", kind}, {"name", entry.Name.Std()}, {"type", entry.Type.Std()},
                {"signature", entry.Signature.Std()}, {"range", Range(source, entry.Range)},
                {"selectionRange", Range(source, entry.SelectionRange)}, {"isStatic", entry.IsStatic},
                {"isConst", entry.IsConst}, {"isRef", entry.IsRef}, {"isCallback", entry.IsCallback},
                {"access", entry.Access.Std()}, {"initializer", Json{{"present", entry.HasInitializer}, {"text", entry.Initializer.Std()}}},
                {"attributes", std::move(attributes)}, {"effective", Json{{"property", entry.Effective.Property},
                    {"readOnly", entry.Effective.ReadOnly}, {"noSerialize", entry.Effective.NoSerialize}, {"hidden", entry.Effective.Hidden}}},
                {"members", std::move(members)}};
        }

        Bool ReadChanges(const Json& params, FieldSettingsChange& change, String& error)
        {
            if (false == params.contains("changes") || false == params["changes"].is_object())
            {
                error = "Field settings require a changes object.";
                return false;
            }
            const Json& changes = params["changes"];
            for (const auto& [key, value] : changes.items())
            {
                if (key != "initializer" && key != "attributes")
                {
                    error = "Only field attributes and the source initializer are editable.";
                    return false;
                }
            }
            if (changes.contains("initializer"))
            {
                const Json& initializer = changes["initializer"];
                if (false == initializer.is_null() && false == initializer.is_string())
                {
                    error = "The initializer must be expression text or null to remove it.";
                    return false;
                }
                change.ChangeInitializer = true;
                change.HasInitializer = initializer.is_string();
                if (change.HasInitializer)
                {
                    change.Initializer = initializer.get<std::string>();
                }
            }
            if (changes.contains("attributes"))
            {
                const Json& attributes = changes["attributes"];
                if (false == attributes.is_array() || attributes.size() > GetFieldAttributeDefinitions().Size())
                {
                    error = "Attributes must be the desired list of recognized field settings.";
                    return false;
                }
                change.ChangeAttributes = true;
                for (const Json& attribute : attributes)
                {
                    if (false == attribute.is_object() || false == attribute.contains("name")
                        || false == attribute["name"].is_string() || false == attribute.contains("arguments")
                        || false == attribute["arguments"].is_array())
                    {
                        error = "Every field attribute requires a name and an expression argument list.";
                        return false;
                    }
                    SummaryAttribute value;
                    value.Name = JsonString(attribute, "name");
                    for (const Json& argument : attribute["arguments"])
                    {
                        if (false == argument.is_string())
                        {
                            error = "Attribute arguments must be expression text.";
                            return false;
                        }
                        value.Arguments.Add(String(argument.get<std::string>()));
                    }
                    change.Attributes.Add(std::move(value));
                }
            }
            return true;
        }
    }

    Json Session::ScriptSummaryRequest(const DocumentAnalysis& document, const Json& params,
        Bool edit, Int32& code, String& error)
    {
        const Json textDocument = params.value("textDocument", Json::object());
        if (false == textDocument.contains("version") || false == textDocument["version"].is_number_integer())
        {
            code = -32602;
            error = "Script summary requests require an exact document version.";
            return nullptr;
        }
        if (textDocument["version"].get<Int64>() != document.Input.Version)
        {
            code = -32801;
            error = "The document changed; refresh the script summary before editing settings.";
            return nullptr;
        }
        if (false == edit)
        {
            const ScriptSummary summary = SummarizeScript(*m_analysis, document);
            Json types = Json::array();
            for (const SummaryEntry& type : summary.Types)
            {
                types.push_back(SummaryJson(document.Source.GetText(), type));
            }
            return Json{{"version", document.Input.Version}, {"valid", summary.Valid}, {"types", std::move(types)}};
        }
        UInt32 offset = 0;
        if (false == params.contains("position") || false == Offset(document.Source.GetText(), params["position"], offset))
        {
            code = -32602;
            error = "A current field selection position in UTF-16 is required.";
            return nullptr;
        }
        FieldSettingsChange change;
        Array<SummaryTextEdit> edits;
        if (false == ReadChanges(params, change, error) || false == EditFieldSettings(document, offset, change, edits, error))
        {
            code = -32602;
            return nullptr;
        }
        Json result = Json::array();
        for (const SummaryTextEdit& value : edits)
        {
            result.push_back(Json{{"range", Range(document.Source.GetText(), value.Range)}, {"newText", value.NewText.Std()}});
        }
        return Json{{"version", document.Input.Version}, {"edits", std::move(result)}};
    }
}
