#pragma once
#if COMPILE_OPEN_GL

#include "Graphics/Renderer.hpp"

#include <map>

#include "Graphics/GL/GLHelpers.hpp"


namespace flex
{
	class MeshPrefab;

	namespace gl
	{
		class GLRenderer : public Renderer
		{
		public:
			GLRenderer(GameContext& gameContext);
			virtual ~GLRenderer();

			virtual void PostInitialize(const GameContext& gameContext) override;

			virtual MaterialID InitializeMaterial(const GameContext& gameContext, const MaterialCreateInfo* createInfo) override;
			virtual RenderID InitializeRenderObject(const GameContext& gameContext, const RenderObjectCreateInfo* createInfo) override;
			virtual void PostInitializeRenderObject(const GameContext& gameContext, RenderID renderID) override;
			virtual DirectionalLightID InitializeDirectionalLight(const DirectionalLight& dirLight) override;
			virtual PointLightID InitializePointLight(const PointLight& PointLight) override;

			virtual DirectionalLight& GetDirectionalLight(DirectionalLightID dirLightID) override;
			virtual PointLight& GetPointLight(PointLightID PointLightID) override;

			virtual void Update(const GameContext& gameContext) override;
			virtual void Draw(const GameContext& gameContext) override;
			virtual void DrawImGuiItems(const GameContext& gameContext) override;

			virtual void ReloadShaders(GameContext& gameContext) override;

			virtual void SetTopologyMode(RenderID renderID, TopologyMode topology) override;
			virtual void SetClearColor(real r, real g, real b) override;

			virtual void OnWindowSize(i32 width, i32 height) override;
			
			virtual void SetRenderObjectVisible(RenderID renderID, bool visible) override;

			virtual void SetVSyncEnabled(bool enableVSync) override;

			virtual u32 GetRenderObjectCount() const override;
			virtual u32 GetRenderObjectCapacity() const override;

			virtual void DescribeShaderVariable(RenderID renderID, const std::string& variableName, i32 size,
				Renderer::Type renderType, bool normalized, i32 stride, void* pointer) override;
			
			virtual void SetSkyboxMaterial(MaterialID skyboxMaterialID) override;
			virtual void SetRenderObjectMaterialID(RenderID renderID, MaterialID materialID) override;

			virtual void Destroy(RenderID renderID) override;
			
			virtual void ImGuiNewFrame() override;

		private:
			void ImGuiRender();

			// TODO: Either use these functions or remove them
			void SetFloat(ShaderID shaderID, const std::string& valName, real val);
			void SetUInt(ShaderID shaderID, const std::string& valName, u32 val);
			void SetVec2f(ShaderID shaderID, const std::string& vecName, const glm::vec2& vec);
			void SetVec3f(ShaderID shaderID, const std::string& vecName, const glm::vec3& vec);
			void SetVec4f(ShaderID shaderID, const std::string& vecName, const glm::vec4& vec);
			void SetMat4f(ShaderID shaderID, const std::string& matName, const glm::mat4& mat);

			void GenerateSkybox(const GameContext& gameContext);
			void GenerateGBuffer(const GameContext& gameContext);

			// Draw all static geometry to the given render object's cubemap texture
			void CaptureSceneToCubemap(const GameContext& gameContext, RenderID cubemapRenderID);
			void GenerateCubemapFromHDREquirectangular(const GameContext& gameContext, MaterialID cubemapMaterialID, const std::string& environmentMapPath);
			void GeneratePrefilteredMapFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID);
			void GenerateIrradianceSamplerFromCubemap(const GameContext& gameContext, MaterialID cubemapMaterialID);
			void GenerateBRDFLUT(const GameContext& gameContext, u32 brdfLUTTextureID, glm::uvec2 BRDFLUTSize);

			void SwapBuffers(const GameContext& gameContext);

			void DrawRenderObjectBatch(const GameContext& gameContext, const std::vector<GLRenderObject*>& batchedRenderObjects, const DrawCallInfo& drawCallInfo);
			void DrawSpriteQuad(const GameContext& gameContext, u32 textureHandle, MaterialID materialID, bool flipVertically = false);

			bool GetLoadedTexture(const std::string& filePath, u32& handle);

			MaterialID GetNextAvailableMaterialID();
			bool GetShaderID(const std::string& shaderName, ShaderID& shaderID);

			GLRenderObject* GetRenderObject(RenderID renderID);
			RenderID GetNextAvailableRenderID() const;
			void InsertNewRenderObject(GLRenderObject* renderObject);
			void UnloadShaders();
			void LoadShaders();

			void GenerateFrameBufferTexture(u32* handle, i32 index, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeFrameBufferTexture(u32 handle, GLint internalFormat, GLenum format, GLenum type, const glm::vec2i& size);
			void ResizeRenderBuffer(u32 handle, const glm::vec2i& size);

			void UpdateMaterialUniforms(const GameContext& gameContext, MaterialID materialID);
			void UpdatePerObjectUniforms(RenderID renderID, const GameContext& gameContext);
			void UpdatePerObjectUniforms(MaterialID materialID, const glm::mat4& model, const GameContext& gameContext);

			void BatchRenderObjects(const GameContext& gameContext);
			void DrawDeferredObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawGBufferQuad(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawForwardObjects(const GameContext& gameContext, const DrawCallInfo& drawCallInfo);
			void DrawOffscreenTexture(const GameContext& gameContext);

			// Returns the next binding that would be used
			u32 BindTextures(Shader* shader, GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);
			// Returns the next binding that would be used
			u32 BindDeferredFrameBufferTextures(GLMaterial* glMaterial, u32 startingBinding = 0);

			std::map<MaterialID, GLMaterial> m_Materials;
			std::map<RenderID, GLRenderObject*> m_RenderObjects;

			bool m_VSyncEnabled;

			// TODO: Convert to map?
			std::vector<GLShader> m_Shaders;
			std::map<std::string, u32> m_LoadedTextures; // Key is filepath, value is texture id

			// TODO: Clean up (make more dynamic)
			u32 viewProjectionUBO;
			u32 viewProjectionCombinedUBO;

			RenderID m_GBufferQuadRenderID;
			VertexBufferData m_gBufferQuadVertexBufferData;
			Transform m_gBufferQuadTransform;
			u32 m_gBufferHandle;
			u32 m_gBufferDepthHandle;

			struct FrameBufferHandle
			{
				u32 id;
				GLenum format;
				GLenum internalFormat;
				GLenum type;
			};

			// TODO: Resize all framebuffers automatically by inserting i32o container
			// TODO: Remove ??
			FrameBufferHandle m_gBuffer_PositionMetallicHandle;
			FrameBufferHandle m_gBuffer_NormalRoughnessHandle;
			FrameBufferHandle m_gBuffer_DiffuseAOHandle;

			FrameBufferHandle m_BRDFTextureHandle;
			glm::uvec2 m_BRDFTextureSize;

			// Everything is drawn to this texture before being drawn to the default 
			// frame buffer through some post-processing effects
			FrameBufferHandle m_OffscreenTextureHandle; 
			u32 m_OffscreenFBO;
			u32 m_OffscreenRBO;

			FrameBufferHandle m_LoadingTextureHandle;
			// TODO: Use a mesh prefab here
			VertexBufferData m_SpriteQuadVertexBufferData;
			Transform m_SpriteQuadTransform;
			RenderID m_SpriteQuadRenderID;
			
			MaterialID m_SpriteMatID;
			MaterialID m_PostProcessMatID;

			u32 m_CaptureFBO;
			u32 m_CaptureRBO;

			glm::mat4 m_CaptureProjection;
			std::array<glm::mat4, 6> m_CaptureViews;

			MaterialID m_SkyBoxMaterialID; // Set by the user via SetSkyboxMaterial
			MeshPrefab* m_SkyBoxMesh = nullptr;
			
			VertexBufferData m_1x1_NDC_QuadVertexBufferData;
			Transform m_1x1_NDC_QuadTransform;
			GLRenderObject* m_1x1_NDC_Quad = nullptr; // A 1x1 quad in NDC space

			std::vector<std::vector<GLRenderObject*>> m_DeferredRenderObjectBatches;
			std::vector<std::vector<GLRenderObject*>> m_ForwardRenderObjectBatches;

			GLRenderer(const GLRenderer&) = delete;
			GLRenderer& operator=(const GLRenderer&) = delete;
		};

		void SetClipboardText(void* userData, const char* text);
		const char* GetClipboardText(void* userData);
	} // namespace gl
} // namespace flex

#endif // COMPILE_OPEN_GL