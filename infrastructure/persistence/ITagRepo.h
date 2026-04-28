#pragma once
#include <qvector.h>
#include "domain/tag/TagInfo.h"

class ITagRepo {
public:
    virtual ~ITagRepo() = default;
    virtual QVector<TagInfo> loadAll() = 0;
    virtual void save(const QVector<TagInfo>& tags) = 0;
    virtual void add(const TagInfo& tag) = 0;
    virtual void remove(quint32 tagId) = 0;
    virtual void update(quint32 tagId, const TagInfo& tag) = 0;
};
