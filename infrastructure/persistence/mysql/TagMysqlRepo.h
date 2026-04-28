#pragma once
#include "infrastructure/persistence/ITagRepo.h"
#include "infrastructure/persistence/mysql/ConnectionPool.h"
#include <memory>

class TagMysqlRepo : public ITagRepo {
public:
    explicit TagMysqlRepo(std::shared_ptr<ConnectionPool> pool);
    QVector<TagInfo> loadAll() override;
    void save(const QVector<TagInfo>& tags) override;
    void add(const TagInfo& tag) override;
    void remove(quint32 tagId) override;
    void update(quint32 tagId, const TagInfo& tag) override;
private:
    void ensureTables();
    std::shared_ptr<ConnectionPool> m_pool;
};
