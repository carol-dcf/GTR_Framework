#pragma once
#include "prefab.h"
#include "fbo.h"
#include "sphericalharmonics.h"


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

	//struct to store probes
	struct sProbe {
		Vector3 pos; //where is located
		Vector3 local; //its ijk pos in the matrix
		int index; //its index in the linear array
		SphericalHarmonics sh; //coeffs
	};


	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner
	class Renderer
	{

	public:

		eRenderMode render_mode;
		ePipelineMode pipeline_mode;
		bool render_alpha;
		std::vector<sProbe> probes;

		FBO gbuffers_fbo;
		FBO illumination_fbo;
		FBO ssao_fbo;
		FBO ssao_blur;
		bool blur_ssao;
		bool hdr;
		bool dithering;
		bool show_probe;

		FBO irr_fbo;
		Texture* probes_texture;
		Vector3 dim = Vector3(8, 6, 12);
		Vector3 start_pos = Vector3(-200, 10, -350);  //(-55, 10, -170)
		Vector3 end_pos = Vector3(550, 250, 450);	  //(180, 150, 80)	
		Vector3 delta;

		std::vector<Vector3> random_points;

		std::vector<RenderCall> renderCalls;

		Renderer();

		//add here your functions
		//...

		void addRenderCall(RenderCall renderCall);
		RenderCall createRenderCall(Matrix44 model, Mesh* mesh, Material* material, float distance_to_camera);
		
		void collectRCsandLights(GTR::Scene* scene, Camera* camera);

		void renderToFBO(GTR::Scene* scene, Camera* camera);

		void renderProbe(Vector3 pos, float size, float* coeffs);

		void updateIrradianceCache(Scene* scene);

		void defineGrid(Scene* scene);
		void computeProbeCoefficients(Scene* scene);
		void uploadProbes();

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
		void renderSceneForward(GTR::Scene* scene, Camera* camera);
		void renderShadow(GTR::Scene* scene, Camera* camera);
		void generateShadowmaps(GTR::Scene* scene);
		void getShadows(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera, Scene* scene = nullptr);

		void resize(int width, int height);
		};

	Texture* CubemapFromHDRE(const char* filename);

};