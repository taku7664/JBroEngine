#include "pch.h"
#include "SceneViewTool.h"

#include "Editor/Editor.h"
#include "Engine/Editor/ImEditor.h"
#include "Engine/GameFramework/Component/GameObject.h"
#include "Engine/GameFramework/Component/SpriteRenderer2D.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Scene/Scene.h"

namespace
{
	Matrix3x2 CalculateWorldTransform(const CScene& scene, EntityId entity)
	{
		const Transform2D* transform = scene.GetComponent<Transform2D>(entity);
		Matrix3x2 worldTransform = transform ? transform->ToMatrix3x2() : Matrix3x2::Identity();

		EntityId parent = scene.GetParent(entity);
		while (INVALID_ENTITY_ID != parent)
		{
			const Transform2D* parentTransform = scene.GetComponent<Transform2D>(parent);
			if (parentTransform)
			{
				worldTransform = worldTransform * parentTransform->ToMatrix3x2();
			}
			parent = scene.GetParent(parent);
		}

		return worldTransform;
	}

	EntityId PickSpriteEntity(const CScene& scene, const Vector2<float>& scenePoint)
	{
		EntityId pickedEntity = INVALID_ENTITY_ID;
		std::int32_t pickedSortOrder = std::numeric_limits<std::int32_t>::min();

		scene.ForEach<GameObject, Transform2D, SpriteRenderer2D>(
			[&scene, &scenePoint, &pickedEntity, &pickedSortOrder](
				EntityId entity,
				const GameObject& gameObject,
				const Transform2D&,
				const SpriteRenderer2D& sprite)
			{
				if (false == gameObject.IsActive)
				{
					return;
				}

				Matrix3x2 inverseTransform;
				if (false == CalculateWorldTransform(scene, entity).TryInvert(inverseTransform))
				{
					return;
				}

				const Vector2<float> localPoint = inverseTransform.TransformPoint(scenePoint);
				if (localPoint.x < -0.5f || localPoint.x > 0.5f || localPoint.y < -0.5f || localPoint.y > 0.5f)
				{
					return;
				}

				if (INVALID_ENTITY_ID == pickedEntity || sprite.SortOrder >= pickedSortOrder)
				{
					pickedEntity = entity;
					pickedSortOrder = sprite.SortOrder;
				}
			});

		return pickedEntity;
	}
}

void CSceneViewTool::OnCreate()
{
	SetTitle("SceneView");
}

void CSceneViewTool::OnDestroy()
{
}

void CSceneViewTool::OnUpdate()
{
}

void CSceneViewTool::OnRenderStay()
{
	ImVec2 viewportSize = ImGui::GetContentRegionAvail();
	viewportSize.x = std::max(viewportSize.x, 1.0f);
	viewportSize.y = std::max(viewportSize.y, 1.0f);

	if (Editor::ImEditor)
	{
		Editor::ImEditor->RequestSceneViewRenderTarget(
			static_cast<std::uint32_t>(viewportSize.x),
			static_cast<std::uint32_t>(viewportSize.y));
	}

	const ImVec2 viewportMin = ImGui::GetCursorScreenPos();
	const ImVec2 viewportMax = viewportMin + viewportSize;
	void* textureID = Editor::ImEditor ? Editor::ImEditor->GetSceneViewTextureID() : nullptr;
	if (textureID)
	{
		ImGui::Image(reinterpret_cast<ImTextureID>(textureID), viewportSize, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
	}
	else
	{
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(viewportMin, viewportMax, IM_COL32(26, 28, 32, 255));
		drawList->AddRect(viewportMin, viewportMax, IM_COL32(70, 76, 86, 255));
		ImGui::InvisibleButton("SceneViewViewport", viewportSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	}

	const bool isViewportClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	if (isViewportClicked && Core::SceneManager)
	{
		SafePtr<CScene> scene = Core::SceneManager->GetActiveScene();
		if (scene)
		{
			const ImVec2 mousePosition = ImGui::GetIO().MousePos;
			const float normalizedX = ((mousePosition.x - viewportMin.x) / viewportSize.x) * 2.0f - 1.0f;
			const float normalizedY = 1.0f - ((mousePosition.y - viewportMin.y) / viewportSize.y) * 2.0f;
			const EntityId pickedEntity = PickSpriteEntity(*scene, Vector2<float>(normalizedX, normalizedY));
			if (INVALID_ENTITY_ID != pickedEntity)
			{
				Editor::SelectEntity(pickedEntity);
			}
			else
			{
				Editor::ClearSelection();
			}
		}
	}

	const bool hasScene = Core::SceneManager.IsValid() && Core::SceneManager->GetActiveScene().IsValid();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImVec2 textPos = viewportMin + ImVec2(12.0f, 10.0f);
	drawList->AddText(textPos, IM_COL32(210, 216, 224, 255), hasScene ? "Active Scene" : "No Active Scene");

	char selectedText[96] = {};
	if (INVALID_ENTITY_ID == Editor::GetSelectedEntity())
	{
		std::snprintf(selectedText, sizeof(selectedText), "Selected: None");
	}
	else
	{
		std::snprintf(selectedText, sizeof(selectedText), "Selected: %llu", static_cast<unsigned long long>(Editor::GetSelectedEntity()));
	}
	drawList->AddText(textPos + ImVec2(0.0f, 20.0f), IM_COL32(150, 158, 170, 255), selectedText);
}
