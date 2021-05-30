#pragma once
#include "prefab.h"
#include "fbo.h"


//forward declarations
class Camera;

namespace GTR {

	class Prefab;
	class Material;

	enum eRenderMode {
		DEFAULT,
		SHOW_TEXTURE,
		SHOW_NORMAL,
		SHOW_AO,
		SHOW_UVS,
		SHOW_MULTI,
		SHOW_DEPTH,
		SHOW_GBUFFERS,
		SHOW_DEFERRED,
		SHOW_SSAO
	};

	enum ePipelineMode {
		DEFERRED,
		FORWARD
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
		ePipelineMode pipeline_mode;
		bool render_alpha;

		FBO gbuffers_fbo;
		FBO illumination_fbo;
		FBO ssao_fbo;
		FBO ssao_blur;
		bool blur_ssao;

		std::vector<Vector3> random_points;

		std::vector<RenderCall> renderCalls;

		Renderer();

		//add here your functions
		//...

		void addRenderCall(RenderCall renderCall);
		RenderCall createRenderCall(Matrix44 model, Mesh* mesh, Material* material, float distance_to_camera);
		
		void collectRCsandLights(GTR::Scene* scene, Camera* camera);

		void renderToFBO(GTR::Scene* scene, Camera* camera);

		void renderToFBOForward(GTR::Scene* scene, Camera* camera);
		void renderToFBODeferred(GTR::Scene* scene, Camera* camera);
		void renderMeshDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		//renders several elements of the scene
		//void renderScene(GTR::Scene* scene, Camera* camera);
		void joinGbuffers(GTR::Scene* scene, Camera* camera);
		void illuminationDeferred(GTR::Scene* scene, Camera* camera);

		void generateSSAO(GTR::Scene* scene, Camera* camera);
		std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
		void renderShadow(GTR::Scene* scene, Camera* camera);
		void generateShadowmaps(GTR::Scene* scene);
		void getShadows(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void resize(int width, int height);
		};

	Texture* CubemapFromHDRE(const char* filename);

};