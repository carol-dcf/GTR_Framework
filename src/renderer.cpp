#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"
#include <algorithm>    // std::sort

#include "application.h"


using namespace GTR;

GTR::RenderCall::RenderCall() {}

GTR::Renderer::Renderer()
{
	render_mode = GTR::eRenderMode::SHOW_TEXTURE;
	//collect entities
	for (int i = 0; i < GTR::Scene::instance->entities.size(); ++i)
	{
		BaseEntity* ent = GTR::Scene::instance->entities[i];
		if (!ent->visible)
			continue;

		//is a light
		if (ent->entity_type == LIGHT)
		{
			LightEntity* lent = (GTR::LightEntity*)ent;
			light_entities.push_back(lent);
		}
	}
}

void GTR::Renderer::addRenderCall(RenderCall renderCall)
{
	renderCalls.push_back(renderCall);
}

RenderCall GTR::Renderer::createRenderCall(Matrix44 model, Mesh* mesh, Material* material, float distance_to_camera)
{
	RenderCall renderCall = GTR::RenderCall();
	renderCall.material = material;
	renderCall.mesh = mesh;
	renderCall.model = model;
	renderCall.distance_to_camera = distance_to_camera;
	return renderCall;
}

void GTR::Renderer::addLightEntity(LightEntity* entity)
{
	light_entities.push_back(entity); entity->scene = GTR::Scene::instance;
}

void GTR::Renderer::collectRCsandLights(GTR::Scene* scene, Camera* camera)
{
	renderCalls.clear();
	light_entities.clear();

	//collect entities
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if (pent->prefab)
				renderPrefab(ent->model, pent->prefab, camera);
		}

		//is a light
		if (ent->entity_type == LIGHT)
		{
			float aspect = Application::instance->window_width / (float)Application::instance->window_height;
			LightEntity* lent = (GTR::LightEntity*)ent;
			light_entities.push_back(lent);
			if (lent->light_type == SPOT) // SPOT LIGHT CAMERA
			{
				Vector3 eye = lent->model.getTranslation(); // camera position
				Vector3 center = lent->model.rotateVector(Vector3(0, 0, 1));
				lent->light_camera->lookAt(eye, eye + center, Vector3(0, 1, 0));
				lent->light_camera->setPerspective(lent->cone_angle, aspect, 1.0f, lent->max_distance);
			}
			else if (lent->light_type == DIRECTIONAL) // DIRECTIONAL LIGHT CAMERA
			{
				Camera* light_camera = lent->light_camera;
				Vector3 eye = lent->model.getTranslation(); // camera position
				Vector3 center = lent->model.rotateVector(Vector3(0, 0, 1));

				//Vector3 eye = Vector3(0,400,0); // camera position
				//Vector3 center = lent->model.rotateVector(Vector3(0, 0, 1));
				//Vector3 camera_eye = Application::instance->scene_camera->eye;
				//Vector3 center = camera_eye - (eye * 1000);

				light_camera->lookAt(eye, eye + center, Vector3(0, 1, 0));
				float a_size = lent->area_size;
				light_camera->setOrthographic(-a_size, a_size, -a_size / aspect, a_size / aspect, 10, 10000);
			}
		}
	}

	//std::sort(std::begin(renderCalls), std::end(renderCalls), less_than_alpha());
	//std::sort(std::begin(renderCalls), std::end(renderCalls), less_than_depth());
	std::sort(std::begin(renderCalls), std::end(renderCalls), sort_alpha_depth());

}


void Renderer::renderToFBO(GTR::Scene* scene, Camera* camera)
{
	float w = Application::instance->window_width;
	float h = Application::instance->window_height;

	// create lights' FBO
	generateShadowmaps(scene);

	// show scene
	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, w, h);
	renderScene(scene, camera);

	if (render_mode == SHOW_LIGHT_MP) {
		// show depth buffers
		glDisable(GL_DEPTH_TEST);
		Shader* zshader = Shader::Get("depth");
		zshader->enable();
		int j = 0;
		for (int i = 0; i < light_entities.size(); ++i) {
			LightEntity* light = light_entities[i];
			if (light != NULL) {
				if (light->light_type != POINT) {
					glViewport(j * w / 5, 0, w / 5, h / 5);
					zshader->setUniform("u_camera_nearfar", Vector2(light->light_camera->near_plane, light->light_camera->far_plane));
					light->shadow_buffer->toViewport(zshader);
					++j;
				}
			}
		}
		zshader->disable();
	}
}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	collectRCsandLights(scene, camera);

	for (int i = 0; i < renderCalls.size(); ++i)
	{
		RenderCall render_call = renderCalls[i];
		renderMeshWithMaterial(render_call.model, render_call.mesh, render_call.material, camera);
	}
}

void GTR::Renderer::renderShadow(GTR::Scene* scene, Camera* camera)
{
	//set the clear color (the background color)
	glClearColor(0, 0, 0, 0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	collectRCsandLights(scene, camera);

	for (int i = 0; i < renderCalls.size(); ++i)
	{
		RenderCall render_call = renderCalls[i];
		getShadows(render_call.model, render_call.mesh, render_call.material, camera);
	}
}

void GTR::Renderer::generateShadowmaps(GTR::Scene* scene)
{
	for (int i = 0; i < light_entities.size(); ++i) {
		LightEntity* light = light_entities[i];

		if (light->light_type != POINT) {
			if (light->fbo.fbo_id == 0)
			{
				light->fbo = FBO();
				light->fbo.setDepthOnly(2048, 2048);
				light->shadow_buffer = new Texture();
			}

			light->fbo.bind();
			glColorMask(false, false, false, false);
			glClear(GL_DEPTH_BUFFER_BIT);

			renderShadow(scene, light->light_camera);

			light->fbo.unbind();

			glColorMask(true, true, true, true);

			light->shadow_buffer = light->fbo.depth_texture;
		}
	}
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	GTR::Scene* scene = GTR::Scene::instance;

	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		BoundingBox world_bounding = transformBoundingBox(node_model, node->mesh->box);

		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize))
		{
			//render node mesh
			//renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			float distance_to_camera = world_bounding.center.distance(camera->eye);
			RenderCall render_Call = createRenderCall(node_model, node->mesh, node->material, distance_to_camera);
			addRenderCall(render_Call);

			//node->mesh->renderBounding(node_model, true);
		}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	Texture* mr_texture = NULL;
	Texture* emissive_texture = NULL;
	Texture* occ_texture = NULL;
	Texture* normal_texture = NULL;
	Texture* shadowmap = NULL;
	bool have_normalmap = true;

	GTR::Scene* scene = GTR::Scene::instance;

	// textures
	texture = material->color_texture.texture;
	emissive_texture = material->emissive_texture.texture;
	mr_texture = material->metallic_roughness_texture.texture;
	normal_texture = material->normal_texture.texture;
	occ_texture = material->occlusion_texture.texture;

	if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (mr_texture == NULL) mr_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (emissive_texture == NULL) emissive_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (occ_texture == NULL) occ_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	//if (normal_texture == NULL) normal_texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (normal_texture == NULL) have_normalmap = false;

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided) glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);

	assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	shader = Shader::Get("texture");
	switch (render_mode)
	{
	case SHOW_TEXTURE: shader = Shader::Get("texture"); break;
	case SHOW_LIGHT_SP: shader = Shader::Get("light_singlepass"); break;
	case SHOW_LIGHT_MP: shader = Shader::Get("light_multipass"); break;
	case SHOW_NORMALS: shader = Shader::Get("normal"); break;
	}

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	float t = getTime();
	shader->setUniform("u_time", t);
	shader->setUniform("u_color", material->color);

	if (texture) shader->setUniform("u_texture", texture, 0);
	if (mr_texture) shader->setUniform("u_mr_texture", mr_texture, 1);
	if (emissive_texture) shader->setUniform("u_emissive_texture", emissive_texture, 2);
	if (occ_texture) shader->setUniform("u_occ_texture", occ_texture, 3);
	if (normal_texture) shader->setUniform("u_normal_texture", normal_texture, 4);
	shader->setUniform("u_have_normalmap", have_normalmap);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	// light information
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_emissive_factor", material->emissive_factor);


	// SINGLE PASS
	if (render_mode == SHOW_LIGHT_SP)
	{
		// lights
		int num_lights = light_entities.size();
		Vector3 light_position[5];
		Vector3 light_color[5];
		Vector3 light_vector[5];
		int light_type[5] = {};
		float max_distances[5];
		float light_intensities[5];
		float light_cos_cutoff[5];
		float light_exponents[5];

		for (int j = 0; j < num_lights; ++j)
		{
			LightEntity* lent = light_entities[j];
			light_color[j] = lent->color;
			light_position[j] = lent->model.getTranslation(); //convert a position from local to world
			light_type[j] = static_cast<int>(lent->light_type);
			if (lent->light_type == SPOT)
				light_vector[j] = lent->model.frontVector();
			else if (lent->light_type == DIRECTIONAL)
				light_vector[j] = lent->model.rotateVector(Vector3(0, 0, -1));
			if (lent->light_type == SPOT) {
				light_cos_cutoff[j] = cos(lent->cone_angle * DEG2RAD);
				light_exponents[j] = lent->exponent;
			}
			max_distances[j] = lent->max_distance;
			light_intensities[j] = lent->intensity;
		}

		shader->setUniform3Array("u_light_pos", (float*)&light_position, 5);
		shader->setUniform3Array("u_light_color", (float*)&light_color, 5);
		shader->setUniform1Array("u_light_type", (int*)&light_type, 5);
		shader->setUniform3Array("u_light_vector", (float*)&light_vector, 5);
		shader->setUniform1("u_num_lights", num_lights);
		shader->setUniform1Array("u_light_maxdist", (float*)&max_distances, 5);
		shader->setUniform1Array("u_light_intensity", (float*)&light_intensities, 5);
		shader->setUniform1Array("u_cosine_cutoff", (float*)&light_cos_cutoff, 5);
		shader->setUniform1Array("u_exponent", (float*)&light_exponents, 5);

		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}

	// MULTI PASS
	else if (render_mode == SHOW_LIGHT_MP)
	{
		//allow to render pixels that have the same depth as the one in the depth buffer
		glDepthFunc(GL_LEQUAL);

		//set blending mode to additive, this will collide with materials with blend...
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		for (int i = 0; i < light_entities.size(); ++i)
		{
			LightEntity* light = light_entities[i];
			//first pass doesn't use blending
			if (i == 0) {
				if (material->alpha_mode == GTR::eAlphaMode::BLEND) {
					glEnable(GL_BLEND);
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				}
				else {
					glBlendFunc(GL_SRC_ALPHA, GL_ONE);
					glDisable(GL_BLEND);
				}
			}
			else {
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				shader->setUniform("u_ambient_light", Vector3(0, 0, 0));
				shader->setUniform("u_emissive_factor", Vector3(0, 0, 0));
			}

			light->setUniforms(shader);

			//render the mesh
			mesh->render(GL_TRIANGLES);
		}

		glDepthFunc(GL_LESS); //as default
	}

	else
		mesh->render(GL_TRIANGLES); //do the draw call that renders the mesh into the screen

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
}

void GTR::Renderer::getShadows(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;

	GTR::Scene* scene = GTR::Scene::instance;

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
		return;
	else
		glDisable(GL_BLEND);

	if (material->two_sided) glDisable(GL_CULL_FACE);
	else glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	shader = Shader::Get("shadow");

	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_color", material->color);
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	Texture* texture = NULL;
	texture = material->color_texture.texture;
	if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (texture) shader->setUniform("u_texture", texture, 0);


	glDepthFunc(GL_LESS); //as default

	//do the draw call that renders the mesh into the screen
	mesh->render(GL_TRIANGLES);

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
}


Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = new HDRE();
	if (!hdre->load(filename))
	{
		delete hdre;
		return NULL;
	}

	/*
	Texture* texture = new Texture();
	texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFaces(0), hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT );
	for(int i = 1; i < 6; ++i)
		texture->uploadCubemap(texture->format, texture->type, false, (Uint8**)hdre->getFaces(i), GL_RGBA32F, i);
	return texture;
	*/
	return NULL;
}

