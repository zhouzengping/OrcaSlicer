#pragma once

#include <list>
#include <string>

#include "configmetadata.hpp"

// key 级展示文本：字典式查表，fieldId = "label" / "full_label" / "tooltip" / "sidetext" / "category"；
// lang 为空串取原文，无译文回落原文
struct IConfigSchemaI18N
{
    virtual ~IConfigSchemaI18N() = default;

    virtual std::string text(const std::string& key, const std::string& fieldId,
                             const std::string& lang) const = 0;

    virtual bool hasField(const std::string& key, const std::string& fieldId) const = 0;

    virtual bool hasTranslation(const std::string& key, const std::string& fieldId,
                                const std::string& lang) const = 0;

    virtual std::list<std::string> enumLabels(const std::string& key, const std::string& lang) const = 0;
};


struct IConfigSchemaGroup
{
    virtual ~IConfigSchemaGroup() = default;

    virtual std::string name() const = 0;
    virtual std::string displayName(const std::string& lang) const = 0;

    virtual std::list<std::string> keys() const = 0;
    virtual std::list<IConfigMetaData*> metaDatas() const = 0;
};


struct IConfigSchemaTab
{
    virtual ~IConfigSchemaTab() = default;

    virtual std::string name() const = 0;
    virtual std::string displayName(const std::string& lang) const = 0;

    virtual std::list<IConfigSchemaGroup*> groups() const = 0;
};


struct IConfigSchemaDisplay
{
    virtual ~IConfigSchemaDisplay() = default;

    virtual std::list<IConfigSchemaTab*> tabs() const = 0;
};
