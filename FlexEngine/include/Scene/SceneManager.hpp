﻿#pragma once

#include <vector>

#include "Scene/BaseScene.hpp"
#include "GameContext.hpp"

namespace flex
{
	class SceneManager
	{
	public:
		SceneManager();
		virtual ~SceneManager();

		void UpdateAndRender(const GameContext& gameContext);

		void AddScene(BaseScene* newScene);

		/* To be called after AddScene */
		void InitializeCurrentScene(const GameContext& gameContext);

		/* To be called after InitializeCurrentScene */
		void PostInitializeCurrentScene(const GameContext& gameContext);

		void RemoveScene(BaseScene* scene, const GameContext& gameContext);

		/* Destroys previous scene if exists, then sets current index (NOTE: Does *not* initialize new scene! */
		bool SetCurrentScene(u32 sceneIndex, const GameContext& gameContext, bool bPrintErrorOnFailure = true);
		bool SetCurrentScene(BaseScene* scene, const GameContext& gameContext, bool bPrintErrorOnFailure = true);
		bool SetCurrentScene(const std::string& sceneFileName, const GameContext& gameContext, bool bPrintErrorOnFailure = true);
		void SetNextSceneActiveAndInit(const GameContext& gameContext);
		void SetPreviousSceneActiveAndInit(const GameContext& gameContext);
		void ReloadCurrentScene(const GameContext& gameContext);

		// Adds all scenes found in scenes directory
		void AddFoundScenes();
		void RemoveDeletedScenes();

		void DeleteScene(const GameContext& gameContext, BaseScene* scene);
		void CreateNewScene(const GameContext& gameContext, const std::string& name, bool bSwitchImmediately);

		u32 CurrentSceneIndex() const;
		BaseScene* CurrentScene() const;
		u32 GetSceneCount() const;

		i32 GetCurrentSceneIndex() const;
		BaseScene* GetSceneAtIndex(i32 index);

		void DestroyAllScenes(const GameContext& gameContext);

	private:
		bool SceneExists(const std::string& fileName) const;

		u32 m_CurrentSceneIndex = u32_max;
		std::vector<BaseScene*> m_Scenes;

		std::string m_SavedDirStr;
		std::string m_DefaultDirStr;

		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

	};
} // namespace flex
