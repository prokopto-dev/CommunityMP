#include "inputactions.hpp"

#include <queue>
#include <set>

#include <components/debug/debuglog.hpp>

#include "luastate.hpp"

namespace LuaUtil
{
    namespace InputAction
    {
        namespace
        {
            std::string_view typeName(Type actionType)
            {
                switch (actionType)
                {
                    case Type::Boolean:
                        return "Boolean";
                    case Type::Number:
                        return "Number";
                    case Type::Range:
                        return "Range";
                    default:
                        throw std::logic_error("Unknown input action type");
                }
            }
        }

        MultiTree::Node MultiTree::insert()
        {
            size_t nextId = size();
            mChildren.push_back({});
            mParents.push_back({});
            return nextId;
        }

        bool MultiTree::validateTree() const
        {
            std::vector<bool> complete(size(), false);
            traverse([&complete](Node node) { complete[node] = true; });
            return std::find(complete.begin(), complete.end(), false) == complete.end();
        }

        template <typename Function>
        void MultiTree::traverse(Function callback) const
        {
            std::queue<Node> nodeQueue;
            std::vector<bool> complete(size(), false);
            for (Node root = 0; root < size(); ++root)
            {
                if (!complete[root])
                    nodeQueue.push(root);
                while (!nodeQueue.empty())
                {
                    Node node = nodeQueue.back();
                    nodeQueue.pop();

                    bool isComplete = true;
                    for (Node parent : mParents[node])
                        isComplete = isComplete && complete[parent];
                    complete[node] = isComplete;
                    if (isComplete)
                    {
                        callback(node);
                        for (Node child : mChildren[node])
                            nodeQueue.push(child);
                    }
                }
            }
        }

        bool MultiTree::multiEdge(Node target, const std::vector<Node>& source)
        {
            mParents[target].reserve(mParents[target].size() + source.size());
            for (Node s : source)
            {
                mParents[target].push_back(s);
                mChildren[s].push_back(target);
            }
            bool validTree = validateTree();
            if (!validTree)
            {
                for (Node s : source)
                {
                    mParents[target].pop_back();
                    mChildren[s].pop_back();
                }
            }
            return validTree;
        }

        namespace
        {
            bool validateActionValue(sol::object value, Type type)
            {
                switch (type)
                {
                    case Type::Boolean:
                        return value.get_type() == sol::type::boolean;
                    case Type::Number:
                        return value.get_type() == sol::type::number;
                    case Type::Range:
                        if (value.get_type() != sol::type::number)
                            return false;
                        double d = value.as<double>();
                        return 0.0 <= d && d <= 1.0;
                }
                throw std::invalid_argument("Unknown action type");
            }
        }

        void Registry::insert(const Info& info)
        {
            if (mIds.find(info.mKey) != mIds.end())
                throw std::domain_error("Action key \"" + info.mKey + "\" is already in use");
            if (info.mKey.empty())
                throw std::domain_error("Action key can't be an empty string");
            if (info.mL10n.empty())
                throw std::domain_error("Localization context can't be empty");
            if (!validateActionValue(info.mDefaultValue, info.mType))
                throw std::logic_error("Invalid value: \"" + LuaUtil::toString(info.mDefaultValue) + "\" for action \""
                    + info.mKey + "\"");
            Id id = mBindingTree.insert();
            mKeys.push_back(info.mKey);
            mIds[std::string(info.mKey)] = id;
            mInfo.push_back(info);
            mHandlers.push_back({});
            mBindings.push_back({});
            mValues.push_back(info.mDefaultValue);
        }

        std::optional<std::string> Registry::nextKey(std::string_view key) const
        {
            auto it = mIds.find(key);
            if (it == mIds.end())
                return std::nullopt;
            auto nextId = it->second + 1;
            if (nextId >= mKeys.size())
                return std::nullopt;
            return mKeys.at(nextId);
        }

        std::optional<Info> Registry::operator[](std::string_view actionKey)
        {
            auto iter = mIds.find(actionKey);
            if (iter == mIds.end())
                return std::nullopt;
            return mInfo[iter->second];
        }

        Registry::Id Registry::safeIdByKey(std::string_view key)
        {
            auto iter = mIds.find(key);
            if (iter == mIds.end())
                throw std::logic_error("Unknown action key: \"" + std::string(key) + "\"");
            return iter->second;
        }

        bool Registry::bind(std::string_view key, const LuaUtil::Callback& callback,
            const std::vector<std::string_view>& dependencies, bool persistent)
        {
            Id id = safeIdByKey(key);
            std::vector<Id> dependencyIds;
            dependencyIds.reserve(dependencies.size());
            for (std::string_view s : dependencies)
                dependencyIds.push_back(safeIdByKey(s));
            bool validEdge = mBindingTree.multiEdge(id, dependencyIds);
            if (validEdge)
                mBindings[id].push_back(Binding{
                    callback,
                    std::move(dependencyIds),
                    persistent,
                });
            return validEdge;
        }

        sol::object Registry::valueOfType(std::string_view key, Type type)
        {
            Id id = safeIdByKey(key);
            Info info = mInfo[id];
            if (info.mType != type)
            {
                std::string message("Attempt to get value of type \"");
                message += typeName(type);
                message += "\" from action \"";
                message += key;
                message += "\" with type \"";
                message += typeName(info.mType);
                message += "\"";
                throw std::logic_error(message);
            }
            return mValues[id];
        }

        void Registry::update(double dt)
        {
            std::vector<sol::object> dependencyValues;
            mBindingTree.traverse([this, &dependencyValues, dt](Id node) {
                sol::main_object newValue = mValues[node];
                std::vector<Binding>& bindings = mBindings[node];
                bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                   [&](const Binding& binding) {
                                       if (!binding.mCallback.isValid())
                                           return true;

                                       dependencyValues.clear();
                                       for (Id parent : binding.mDependencies)
                                           dependencyValues.push_back(mValues[parent]);
                                       try
                                       {
                                           newValue = sol::main_object(
                                               binding.mCallback.call(dt, newValue, sol::as_args(dependencyValues)));
                                       }
                                       catch (std::exception& e)
                                       {
                                           if (!validateActionValue(newValue, mInfo[node].mType))
                                               Log(Debug::Error)
                                                   << "Error due to invalid value of action \"" << mKeys[node]
                                                   << "\"(\"" << LuaUtil::toString(newValue) << "\"): " << e.what();
                                           else
                                               Log(Debug::Error) << "Error in callback: " << e.what();
                                       }
                                       return false;
                                   }),
                    bindings.end());

                if (!validateActionValue(newValue, mInfo[node].mType))
                    Log(Debug::Error) << "Invalid value of action \"" << mKeys[node]
                                      << "\": " << LuaUtil::toString(newValue);
                if (mValues[node] != newValue)
                {
                    mValues[node] = sol::object(newValue);
                    std::vector<Handler>& handlers = mHandlers[node];
                    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                       [&](const Handler& handler) {
                                           if (!handler.mCallback.isValid())
                                               return true;
                                           handler.mCallback.tryCall(newValue);
                                           return false;
                                       }),
                        handlers.end());
                }
            });
        }

        void Registry::clear(bool force)
        {
            struct PersistentBinding
            {
                LuaUtil::Callback mCallback;
                std::vector<std::string> mDependencies;
            };
            struct PersistentAction
            {
                Info mInfo;
                std::vector<Handler> mHandlers;
                std::vector<PersistentBinding> mBindings;
            };

            std::vector<PersistentAction> actionsToKeep;
            if (!force)
            {
                for (Id id = 0; id < mInfo.size(); ++id)
                {
                    if (!mInfo[id].mPersistent)
                        continue;

                    PersistentAction action{ mInfo[id], {}, {} };
                    for (const Handler& handler : mHandlers[id])
                    {
                        if (handler.mPersistent && handler.mCallback.isValid())
                            action.mHandlers.push_back(handler);
                    }

                    for (const Binding& binding : mBindings[id])
                    {
                        if (!binding.mPersistent || !binding.mCallback.isValid())
                            continue;

                        PersistentBinding persistentBinding{ binding.mCallback, {} };
                        bool dependenciesArePersistent = true;
                        persistentBinding.mDependencies.reserve(binding.mDependencies.size());
                        for (Id dependencyId : binding.mDependencies)
                        {
                            if (dependencyId >= mInfo.size() || !mInfo[dependencyId].mPersistent)
                            {
                                dependenciesArePersistent = false;
                                break;
                            }
                            persistentBinding.mDependencies.push_back(mKeys[dependencyId]);
                        }

                        if (dependenciesArePersistent)
                            action.mBindings.push_back(std::move(persistentBinding));
                    }

                    actionsToKeep.push_back(std::move(action));
                }
            }
            mKeys.clear();
            mIds.clear();
            mInfo.clear();
            mHandlers.clear();
            mBindings.clear();
            mValues.clear();
            mBindingTree.clear();
            if (!force)
            {
                for (const PersistentAction& action : actionsToKeep)
                    insert(action.mInfo);

                for (PersistentAction& action : actionsToKeep)
                {
                    const Id id = safeIdByKey(action.mInfo.mKey);
                    mHandlers[id] = std::move(action.mHandlers);

                    for (const PersistentBinding& binding : action.mBindings)
                    {
                        std::vector<std::string_view> dependencies;
                        dependencies.reserve(binding.mDependencies.size());
                        for (const std::string& dependency : binding.mDependencies)
                            dependencies.push_back(dependency);
                        bind(action.mInfo.mKey, binding.mCallback, dependencies, true);
                    }
                }
            }
        }
    }

    namespace InputTrigger
    {
        Registry::Id Registry::safeIdByKey(std::string_view key)
        {
            auto it = mIds.find(key);
            if (it == mIds.end())
                throw std::domain_error("Unknown trigger key \"" + std::string(key) + "\"");
            return it->second;
        }

        void Registry::insert(const Info& info)
        {
            if (mIds.find(info.mKey) != mIds.end())
                throw std::domain_error("Trigger key \"" + info.mKey + "\" is already in use");
            if (info.mKey.empty())
                throw std::domain_error("Trigger key can't be an empty string");
            if (info.mL10n.empty())
                throw std::domain_error("Localization context can't be empty");
            Id id = mIds.size();
            mIds[info.mKey] = id;
            mInfo.push_back(info);
            mHandlers.push_back({});
        }

        std::optional<Info> Registry::operator[](std::string_view key)
        {
            auto iter = mIds.find(key);
            if (iter == mIds.end())
                return std::nullopt;
            return mInfo[iter->second];
        }

        void Registry::registerHandler(std::string_view key, const LuaUtil::Callback& callback, bool persistent)
        {
            Id id = safeIdByKey(key);
            mHandlers[id].push_back(Handler{ callback, persistent });
        }

        void Registry::activate(std::string_view key)
        {
            Id id = safeIdByKey(key);
            std::vector<Handler>& handlers = mHandlers[id];
            handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                               [&](const Handler& handler) {
                                   if (!handler.mCallback.isValid())
                                       return true;
                                   handler.mCallback.tryCall();
                                   return false;
                               }),
                handlers.end());
        }

        void Registry::clear(bool force)
        {
            struct PersistentTrigger
            {
                Info mInfo;
                std::vector<Handler> mHandlers;
            };

            std::vector<PersistentTrigger> triggersToKeep;
            if (!force)
            {
                for (Id id = 0; id < mInfo.size(); ++id)
                {
                    if (!mInfo[id].mPersistent)
                        continue;

                    PersistentTrigger trigger{ mInfo[id], {} };
                    for (const Handler& handler : mHandlers[id])
                    {
                        if (handler.mPersistent && handler.mCallback.isValid())
                            trigger.mHandlers.push_back(handler);
                    }
                    triggersToKeep.push_back(std::move(trigger));
                }
            }
            mInfo.clear();
            mHandlers.clear();
            mIds.clear();
            if (!force)
            {
                for (PersistentTrigger& trigger : triggersToKeep)
                {
                    insert(trigger.mInfo);
                    mHandlers[safeIdByKey(trigger.mInfo.mKey)] = std::move(trigger.mHandlers);
                }
            }
        }
    }
}
