// test_db.cpp —— 数据库层自动化测试

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "common/config.h"
#include "data/db.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        if (cond) {                                                        \
            ++g_pass;                                                      \
        } else {                                                           \
            ++g_fail;                                                      \
            std::printf("  [FAIL] %s  (第 %d 行)\n", msg, __LINE__);        \
        }                                                                  \
    } while (0)

#define CHECK_THROWS(expr, msg)                                            \
    do {                                                                   \
        bool thrown = false;                                               \
        try { expr; } catch (const std::exception&) { thrown = true; }     \
        if (thrown) { ++g_pass; }                                          \
        else { ++g_fail; std::printf("  [FAIL] %s (应抛异常但没抛) (第 %d 行)\n", msg, __LINE__); } \
    } while (0)

#define SECTION(name) std::printf("\n=== %s ===\n", name)

int main() {
    const char* db_path = "/tmp/test_book_cpp.db";
    std::remove(db_path);
    std::remove((std::string(db_path) + "-wal").c_str());
    std::remove((std::string(db_path) + "-shm").c_str());
    setenv("BOOK_DB_PATH", db_path, 1);

    db::init();
    CHECK(!db::current_path().empty(), "数据库路径已设置");

    SECTION("1. 初始槽位");
    {
        auto slots = db::slot_list();
        CHECK(slots.size() == 16, "应有 16 个书架格子");
        CHECK(slots[0].coord_y == 11, "第一个格子应是 11");
        CHECK(slots[15].coord_y == 28, "最后一个格子应是 28");

        bool all_free = true;
        for (const auto& s : slots) {
            if (s.is_occupied != 0) all_free = false;
        }
        CHECK(all_free, "初始时所有格子都是空的");
    }

    SECTION("2. 书目 CRUD");
    int64_t book_id = 0;
    {
        auto b = db::book_create("三体", "文学", "", "刘慈欣的科幻小说", "刘慈欣");
        book_id = b.id;
        CHECK(b.id > 0, "创建后应拿到正数 ID");
        CHECK(b.title == "三体", "书名正确");

        auto got = db::book_get(book_id);
        CHECK(got.has_value(), "能按 ID 查到");
        CHECK(got->category == "文学", "分类正确");
        CHECK(got->author == "刘慈欣", "作者正确");

        db::BookUpdate upd;
        upd.title = std::string("三体（修订版）");
        bool ok = db::book_update(book_id, upd);
        CHECK(ok, "更新应成功");

        auto updated = db::book_get(book_id);
        CHECK(updated->title == "三体（修订版）", "更新后书名变了");
        CHECK(updated->category == "文学", "没改的字段保持原值");

        auto page = db::book_list(1, 10);
        CHECK(page.total == 1, "总数应为 1");
        CHECK(page.items.size() == 1, "当前页应有 1 条");
        CHECK(page.pages == 1, "总页数应为 1");

        auto found = db::book_search("三体");
        CHECK(found.size() == 1, "搜索能命中");
        auto not_found = db::book_search("不存在的书名");
        CHECK(not_found.empty(), "搜不到时返回空");
    }

    SECTION("3. 实例与槽位分配");
    int64_t inst_id = 0;
    {
        auto inst = db::instance_create(book_id, 0, 0, 2);
        inst_id = inst.instance_id;
        CHECK(inst.instance_id > 0, "实例 ID 有效");
        CHECK(inst.status == 2, "初始状态=待上架");
        CHECK(inst.title == "三体（修订版）", "JOIN 出了书名");

        auto alloc = db::scan_allocate(book_id, "文学", 3);
        CHECK(alloc.slot_id > 0, "拿到了格子");
        CHECK(alloc.coord_x == 1, "文学区在 1 层");
        CHECK(alloc.coord_y >= 15 && alloc.coord_y <= 18, "文学区坐标范围 15-18");
        CHECK(alloc.remark.find("文学区") == 0, "备注是文学区");

        auto after = db::instance_get(inst_id);
        CHECK(after->status == 3, "状态改为在架");
        CHECK(after->slot_id == alloc.slot_id, "实例绑定了格子");
        CHECK(after->coord_y == alloc.coord_y, "JOIN 出了坐标");

        auto slot = db::slot_get(alloc.slot_id);
        CHECK(slot->is_occupied == 1, "格子标记为已占用");
    }

    SECTION("4. 借书流程");
    int64_t user_id = 0;
    {
        auto u = db::user_create("openid_test_001", "测试用户", "", 0);
        user_id = u.user_id;
        CHECK(u.user_id > 0, "用户创建成功");
        CHECK(u.borrow_count == 0, "初始借书数为 0");

        auto borrowed = db::borrow_instance(inst_id, user_id);
        CHECK(borrowed.status == 1, "状态改为已借出");
        CHECK(borrowed.borrower_id == user_id, "记录了借阅人");
        CHECK(!borrowed.due_date.empty(), "设置了应还日期");
        CHECK(borrowed.slot_id == 0, "槽位已清空");

        auto slots = db::slot_list();
        int free_count = 0;
        for (const auto& s : slots) {
            if (s.is_occupied == 0) ++free_count;
        }
        CHECK(free_count == 16, "借走后所有格子都空出来了");

        CHECK(db::user_borrow_count(user_id) == 1, "借书数变成 1");

        CHECK_THROWS(db::borrow_instance(inst_id, user_id), "重复借同一本书应报错");
    }

    SECTION("5. 还书流程");
    {
        auto ret = db::return_instance(inst_id);
        CHECK(ret.instance.status == 2, "状态改为待上架");
        CHECK(ret.overdue_days == 0, "刚借就还，不逾期");
        CHECK(ret.fine == 0.0, "不产生罚款");
        CHECK(db::user_borrow_count(user_id) == 0, "借书数回到 0");

        // 未借出的书不能还
        CHECK_THROWS(db::return_instance(inst_id), "没借的书不能还");
    }

    SECTION("6. 重新上架");
    {
        auto re = db::re_shelve_instance(inst_id, "文学");
        CHECK(re.slot_id > 0, "重新分配到格子");
        CHECK(re.coord_y >= 15 && re.coord_y <= 18, "还是文学区");

        auto inst = db::instance_get(inst_id);
        CHECK(inst->status == 3, "状态回到在架");
    }

    SECTION("7. 借阅规则：上限 / 冻结");
    {
        std::vector<int64_t> ids;
        for (int i = 0; i < 6; ++i) {
            auto b = db::book_create("测试书" + std::to_string(i), "科技");
            auto inst = db::instance_create(b.id, 0, 0, 2);
            db::scan_allocate(b.id, "科技", 3);
            ids.push_back(inst.instance_id);
        }

        int success = 0;
        for (size_t i = 0; i < ids.size(); ++i) {
            try {
                db::borrow_instance(ids[i], user_id);
                ++success;
            } catch (const std::exception&) {
            }
        }
        CHECK(success == cfg::MAX_BORROW_COUNT,
              ("最多借 " + std::to_string(cfg::MAX_BORROW_COUNT) + " 本").c_str());

        // 冻结用户后不能再借
        db::UserUpdate fu;
        fu.is_frozen = 1;
        db::user_update(user_id, fu);
        CHECK(db::user_is_frozen(user_id), "用户已冻结");

        auto b2 = db::book_create("冻结后测试", "科技");
        db::instance_create(b2.id, 0, 0, 2);
        db::scan_allocate(b2.id, "科技", 3);
        auto inst2 = db::instance_get_by_book_id_any(b2.id);
        CHECK_THROWS(db::borrow_instance(inst2->instance_id, user_id),
                     "冻结用户借书应被拒");

        db::UserUpdate uf;
        uf.is_frozen = 0;
        db::user_update(user_id, uf);
        CHECK(!db::user_is_frozen(user_id), "已解冻");
    }

    SECTION("8. 罚款");
    {
        db::user_add_fine(user_id, 2.5);
        CHECK(db::user_get_fine(user_id) == 2.5, "罚款累加正确");

        db::user_add_fine(user_id, 1.25);
        CHECK(db::user_get_fine(user_id) == 3.75, "再次累加正确");
    }

    SECTION("9. 操作日志");
    {
        auto lg = db::log_create(inst_id, user_id, "BORROW", "借阅《三体》", 1, "测试用户");
        CHECK(lg.log_id > 0, "日志写入成功");

        auto got = db::log_get(lg.log_id);
        CHECK(got.has_value(), "能按 ID 查日志");
        CHECK(got->action_type == "BORROW", "动作类型正确");

        auto page = db::log_page(1, 10);
        CHECK(page.total == 1, "日志总数 1");

        auto by_inst = db::log_by_instance(inst_id);
        CHECK(by_inst.size() == 1, "按实例查有 1 条");
    }

    SECTION("10. 并发抢格子（乐观锁验证）");
    {
        for (auto& s : db::slot_list()) {
            if (s.is_occupied) db::slot_release(s.slot_id);
        }
        auto all_instances = db::instance_page(1, 1000);
        for (auto& inst : all_instances.items) {
            if (inst.slot_id != 0) {
                db::InstanceUpdate u;
                u.slot_id = static_cast<int64_t>(0);
                db::instance_update(inst.instance_id, u);
            }
        }

        std::vector<int64_t> book_ids;
        for (int i = 0; i < 24; ++i) {
            auto b = db::book_create("并发书" + std::to_string(i), "其他");
            db::instance_create(b.id, 0, 0, 2);
            book_ids.push_back(b.id);
        }

        std::atomic<int> success{0};
        std::atomic<int> failed{0};

        auto worker = [&](int start) {
            for (int i = 0; i < 6; ++i) {
                int idx = start + i;
                if (idx >= static_cast<int>(book_ids.size())) break;
                try {
                    db::scan_allocate(book_ids[idx], "其他", 3);
                    ++success;
                } catch (const std::exception&) {
                    ++failed;
                }
            }
        };

        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back(worker, t * 6);
        }
        for (auto& th : threads) {
            th.join();
        }

        std::printf("  成功 %d 次，失败 %d 次\n", success.load(), failed.load());
        CHECK(success.load() == 16, "恰好 16 次成功（等于格子数）");
        CHECK(success.load() + failed.load() == 24, "24 次尝试都有结果");

        auto final_slots = db::slot_list();
        int occupied = 0;
        for (auto& s : final_slots) {
            if (s.is_occupied) ++occupied;
        }
        std::printf("  实测：共 %zu 个格子，其中 %d 个被占用\n",
                    final_slots.size(), occupied);
        CHECK(occupied == 16, "16 个格子全部被占且只占一次");
    }

    std::printf("\n========================================\n");
    std::printf("通过 %d 项，失败 %d 项\n", g_pass, g_fail);
    std::printf("========================================\n");

    return g_fail == 0 ? 0 : 1;
}
