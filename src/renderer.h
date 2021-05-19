#pragma once
#include "prefab.h"
#include "fbo.h"


//forward declarations
class Camera;

namespace GTR {

	class Prefab;
	class Material;

	enum eRenderMode {
		SHOW_TEXTURE,
		SHOW_LIGHT_SP,
		SHOW_LIGHT_MP,
		SHOW_NORMALS,
		SHOW_DEPTH
	};


	class RenderCall
	{
	public:
		Matrix44 model;
		Mesh* mesh;
		Material* material;

		float distance_to_camera;

		RenderCall();
	};

	struct less_than_alpha
	{
		inline bool operator() (RenderCall& a, RenderCall& b)
		{
			if (10 < 20)
				return (a.material->alpha_mode <= b.material->alpha_mode);
		}
	};

	struct less_than_depth
	{
		inline bool operator() (RenderCall& a, RenderCall& b)
		{
			if (a.material->alpha_mode == GTR::eAlphaMode::BLEND && b.material->alpha_mode == GTR::eAlphaMode::BLEND)
				return (a.distance_to_camera > b.distance_to_camera);
			return (a.distance_to_camera <= b.distance_to_camera);
		}
	};

	struct sort_alpha_depth
	{
		inline bool operator() (RenderCall& a, RenderCall& b)
		{
			if (a.material->alpha_mode != BLEND && b.material->alpha_mode != BLEND)
				return (a.distance_to_camera < b.distance_to_camera);
			else if (a.material->alpha_mode != BLEND && b.material->alpha_mode == BLEND)
				return true;
			else if (a.material->alpha_mode == BLEND && b.material->alpha_mode != BLEND)
				return false;
			else
				return (a.distance_to_camera > b.distance_to_camera);
		}
	};


	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:


		eRenderMode render_mode;
		FBO fbo;
		Texture* color_buffer;
		std::vector<RenderCall> renderCalls;
		std::vector<LightEntity*> light_entities;

		Renderer();

		//add here your functions
		//...

		void addRenderCall(RenderCall renderCall);
		RenderCall createRenderCall(Matrix44 model, Mesh* mesh, Material* material, float distance_to_camera);

		void addLightEntity(LightEntity* entity);

		void collectRCsandLights(GTR::Scene* scene, Camera* camera);

		void renderToFBO(GTR::Scene* scene, Camera* camera);

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
		void renderShadow(GTR::Scene* scene, Camera* camera);
		void generateShadowmaps(GTR::Scene* scene);


		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void getShadows(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
	};

	Texture* CubemapFromHDRE(const char* filename);

};