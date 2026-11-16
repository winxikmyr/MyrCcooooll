// src/modules/friend_manager.h
#pragma once
#include <string>
#include <vector>
#include <algorithm>

class FriendManager {
public:
    static FriendManager& Get() {
        static FriendManager instance;
        return instance;
    }

    bool IsFriend(const std::string& name) const {
        if (name.empty()) return false;
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(tolower(c));
        for (const auto& f : m_friends) {
            std::string fLower = f;
            for (char& c : fLower) c = static_cast<char>(tolower(c));
            if (fLower == lower) return true;
        }
        return false;
    }

    void AddFriend(const std::string& name) {
        if (!name.empty() && !IsFriend(name)) {
            m_friends.push_back(name);
        }
    }

    void RemoveFriend(const std::string& name) {
        if (name.empty()) return;
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(tolower(c));
        m_friends.erase(
            std::remove_if(m_friends.begin(), m_friends.end(), [&](const std::string& f) {
                std::string fLower = f;
                for (char& c : fLower) c = static_cast<char>(tolower(c));
                return fLower == lower;
            }),
            m_friends.end()
        );
    }

    bool ToggleFriend(const std::string& name) {
        if (IsFriend(name)) {
            RemoveFriend(name);
            return false;
        } else {
            AddFriend(name);
            return true;
        }
    }

    const std::vector<std::string>& GetFriends() const {
        return m_friends;
    }

private:
    FriendManager() = default;
    std::vector<std::string> m_friends;
};

