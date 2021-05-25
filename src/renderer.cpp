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
	render_mode = GTR::eRenderMode::DEFAULT;
	pipeline_mode = GTR::ePipelineMode::FORWARD;
	gbuffers_fbo = FBO();
	gbuffers_fbo.create(Application::instance->window_width, Application::instance->window_height, 3, GL_RGBA, GL_FLOAT, true);

	ssao_fbo = FBO();
	ssao_fbo.create(Application::instance->window_width, Application::instance->window_height);
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

void GTR::Renderer::collectRCsandLights(GTR::Scene* scene, Camera* camera)
{
	renderCalls.clear();
	scene->l_entities.clear();

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
			scene->l_entities.push_back(lent);
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

				light_camera->lookAt(eye, eye + center, Vector3(0, 1, 0));
				float a_size = lent->area_size;
				light_camera->setOrthographic(-a_size, a_size, -a_size / aspect, a_size / aspect, 10, 10000);
			}

			/*if (lent->light_type == eLightType::SPOT) {
				cam->lookAt(lent->model.getTranslation(), lent->model.getTranslation() + lent->target, Vector3(0.f, 1.f, 0.f));
				cam->setPerspective(lent->cone_angle, Application::instance->window_width / (float)Application::instance->window_height, 1.0f, 10000.f);
			}
			else if (lent->light_type == eLightType::DIRECTIONAL) {
				cam->lookAt(lent->model.getTranslation(), Vector3(0.0f, 0.0f, 0.0f), Vector3(-1.0f, -1.0f, 0.f));
				cam->setOrthographic(-600, 600, -600, 600, -600, 600);
			}
			else if (lent->light_type == eLightType::POINT) {
				continue;
			}*/
		}
	}

	//std::sort(std::begin(renderCalls), std::end(renderCalls), less_than_alpha());
	//std::sort(std::begin(renderCalls), std::end(renderCalls), less_than_depth());
	std::sort(std::begin(renderCalls), std::end(renderCalls), sort_alpha_depth());

}

void Renderer::renderToFBOForward(GTR::Scene* scene, Camera* camera)
{
	float w = Application::instance->window_width;
	float h = Application::instance->window_height;

	// create lights' FBO
	generateShadowmaps(scene);

	// show scene
	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, w, h);
	renderScene(scene, camera);
}

void Renderer::renderToFBODeferred(GTR::Scene* scene, Camera* camera) {
	
	if (render_mode == SHOW_SSAO) {
		generateSSAO(scene, camera);
		ssao_fbo.color_textures[0]->toViewport();
	}

	generateShadowmaps(scene);

	gbuffers_fbo.bind();

	gbuffers_fbo.enableSingleBuffer(0);

	//clear GB0 with the color (and depth)
	glClearColor(0.1, 0.1, 0.1, 1.0);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	//and now enable the second GB to clear it to black
	gbuffers_fbo.enableSingleBuffer(1);
	glClearColor(0.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	//enable all buffers back
	gbuffers_fbo.enableAllBuffers();

	renderScene(scene, camera);

	gbuffers_fbo.unbind();

	Shader* shader = Shader::Get("depth");
	shader->enable();
	shader->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));

	float w = Application::instance->window_width;
	float h = Application::instance->window_height;

	if (render_mode == SHOW_GBUFFERS) {
		glViewport(0.0f, 0.0f, w / 2, h / 2);
		gbuffers_fbo.color_textures[0]->toViewport();
		glViewport(w / 2, 0.0f, w / 2, h / 2);
		gbuffers_fbo.color_textures[1]->toViewport();
		glViewport(0.0f, h / 2, w / 2, h / 2);
		gbuffers_fbo.color_textures[2]->toViewport();
		glViewport(w / 2, h / 2, w / 2, h / 2);
		gbuffers_fbo.depth_texture->toViewport(shader);
	}
	else { // show deferred all together
		//create and FBO
		glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

		illumination_fbo = FBO();
		//create 3 textures of 4 components
		illumination_fbo.create(w, h, 1, GL_RGB, GL_UNSIGNED_BYTE, false);

		//start rendering to the illumination fbo
		illumination_fbo.bind();

		//joinGbuffers(scene, camera);
		illuminationDeferred(scene, camera);

		illumination_fbo.unbind();
		//be sure blending is not active
		glDisable(GL_BLEND);

		Shader* ambient_shader = Shader::Get("add_ambient");
		ambient_shader->enable();
		ambient_shader->setUniform("u_ambient_light", scene->ambient_light);
		ambient_shader->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
		ambient_shader->setUniform("u_metallic_roughness_texture", gbuffers_fbo.color_textures[2], 1);

		glViewport(0.0f, 0.0f, w, h);
		gbuffers_fbo.color_textures[0]->toViewport(ambient_shader);
		glEnable(GL_BLEND);
		illumination_fbo.color_textures[0]->toViewport();
		ambient_shader->disable();

	}
	shader->disable();
	
	glDisable(GL_BLEND);

}

void Renderer::illuminationDeferred(GTR::Scene* scene, Camera* camera) {

	float w = Application::instance->window_width;
	float h = Application::instance->window_height;
	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	//we need a fullscreen quad
	//Mesh* quad = Mesh::getQuad();
	Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", true);

	Shader* sh = Shader::Get("deferred_ws");

	sh->enable();
	//pass the gbuffers to the shader
	sh->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
	sh->setUniform("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
	sh->setUniform("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
	sh->setUniform("u_depth_texture", gbuffers_fbo.depth_texture, 3);

	//pass the inverse projection of the camera to reconstruct world pos.
	sh->setUniform("u_inverse_viewprojection", inv_vp);
	//pass the inverse window resolution, this may be useful
	sh->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

	sh->setUniform("u_ambient_light", scene->ambient_light);
	sh->setUniform("u_viewprojection", camera->viewprojection_matrix);
	sh->setUniform("u_camera_eye", camera->eye);

	bool first = true;
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);

	for (int i = 0; i < scene->l_entities.size(); ++i) {
		LightEntity* lent = scene->l_entities[i];

		if (!lent->visible) continue;

		lent->setUniforms(sh);

		if (i == 0) {
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_BLEND);
		}
		else {
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE);
		}

		if (lent->light_type == POINT || lent->light_type == SPOT)
		{
			Matrix44 m;
			m.setTranslation(lent->model.getTranslation().x, lent->model.getTranslation().y, lent->model.getTranslation().z);
			//and scale it according to the max_distance of the light
			m.scale(lent->max_distance, lent->max_distance, lent->max_distance);

			//pass the model to the shader to render the sphere
			sh->setUniform("u_model", m);

			sphere->render(GL_TRIANGLES);
		}
		if (lent->light_type == DIRECTIONAL) {
			Mesh* quad = Mesh::getQuad();

			Shader* s = Shader::Get("deferred");
			s->enable();
			lent->setUniforms(s);

			s->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
			s->setUniform("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
			s->setUniform("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
			s->setUniform("u_depth_texture", gbuffers_fbo.depth_texture, 3);

			//pass the inverse projection of the camera to reconstruct world pos.
			s->setUniform("u_inverse_viewprojection", inv_vp);
			//pass the inverse window resolution, this may be useful
			s->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

			s->setUniform("u_ambient_light", scene->ambient_light);
			s->setUniform("u_viewprojection", camera->viewprojection_matrix);


			quad->render(GL_TRIANGLES);
			s->disable();
		}
	}

	glFrontFace(GL_CCW);

}

void Renderer::generateSSAO(GTR::Scene* scene, Camera* camera)
{
	gbuffers_fbo.depth_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	//start rendering inside the ssao texture
	ssao_fbo.bind();

	Mesh* quad = Mesh::getQuad();

	Matrix44 invvp = camera->viewprojection_matrix;
	invvp.inverse();

	//get the shader for SSAO (remember to create it using the atlas)
	Shader* shader = Shader::Get("ssao");
	shader->enable();

	//send info to reconstruct the world position
	shader->setUniform("u_inverse_viewprojection", invvp);
	shader->setTexture("u_depth_texture", gbuffers_fbo.depth_texture, 0);
	//we need the pixel size so we can center the samples 
	shader->setUniform("u_iRes", Vector2(1.0 / (float)gbuffers_fbo.depth_texture->width,
		1.0 / (float)gbuffers_fbo.depth_texture->height));
	//we will need the viewprojection to obtain the uv in the depthtexture of any random position of our world
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);

	std::vector<Vector3> random_points = generateSpherePoints(64, 10, true);

	//send random points so we can fetch around
	shader->setUniform3Array("u_points", (float*)&random_points[0],
		random_points.size());

	//render fullscreen quad
	quad->render(GL_TRIANGLES);

	//stop rendering to the texture
	ssao_fbo.unbind();
}

std::vector<Vector3> Renderer::generateSpherePoints(int num, float radius, bool hemi)
{
	std::vector<Vector3> points;
	points.resize(num);
	for (int i = 0; i < num; i += 3)
	{
		Vector3& p = points[i];
		float u = random();
		float v = random();
		float theta = u * 2.0 * PI;
		float phi = acos(2.0 * v - 1.0);
		float r = cbrt(random() * 0.9 + 0.1) * radius;
		float sinTheta = sin(theta);
		float cosTheta = cos(theta);
		float sinPhi = sin(phi);
		float cosPhi = cos(phi);
		p.x = r * sinPhi * cosTheta;
		p.y = r * sinPhi * sinTheta;
		p.z = r * cosPhi;
		if (hemi && p.z < 0)
			p.z *= -1.0;
	}
	return points;
}

void Renderer::renderToFBO(GTR::Scene* scene, Camera* camera) {

	switch (pipeline_mode) {
	case FORWARD: renderToFBOForward(scene, camera); break;
	case DEFERRED: renderToFBODeferred(scene, camera); break;
	}
}

void Renderer::renderMeshDeferred(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera) {

	Shader* shader = Shader::Get("multi");
	Texture* texture = NULL;
	Texture* normal_texture = NULL;
	Texture* mat_properties_texture = NULL;

	texture = material->color_texture.texture;
	if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture

	bool read_normal = true;
	normal_texture = material->normal_texture.texture;
	if (!normal_texture) read_normal = false;

	mat_properties_texture = material->metallic_roughness_texture.texture;
	if (mat_properties_texture == NULL) mat_properties_texture = Texture::getWhiteTexture(); //a 1x1 white texture

	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		return;
	}
	else
		glDisable(GL_BLEND);

	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	shader->enable();

	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	shader->setUniform("u_emissive_factor", material->emissive_factor);
	if (texture) shader->setUniform("u_texture", texture, 0);
	if (normal_texture) shader->setUniform("u_normal_texture", normal_texture, 1);
	if (mat_properties_texture) shader->setUniform("u_mat_properties_texture", mat_properties_texture, 2);
	shader->setUniform("u_read_normal", read_normal);
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	mesh->render(GL_TRIANGLES);
	shader->disable();
	glDisable(GL_BLEND);
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
		if (pipeline_mode == FORWARD)
			renderMeshWithMaterial(render_call.model, render_call.mesh, render_call.material, camera);
		else
			renderMeshDeferred(render_call.model, render_call.mesh, render_call.material, camera);
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
	for (int i = 0; i < scene->l_entities.size(); ++i) {
		LightEntity* light = scene->l_entities[i];

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
	switch (render_mode) {
		case SHOW_NORMAL: shader = Shader::Get("normal"); break;
		case SHOW_UVS: shader = Shader::Get("uvs"); break;
		case SHOW_TEXTURE: shader = Shader::Get("texture"); break;
		case SHOW_AO: shader = Shader::Get("occlusion"); break;
		case DEFAULT: shader = Shader::Get("light_singlepass"); break;
		case SHOW_MULTI: shader = Shader::Get("light_multipass"); break;
		case SHOW_DEPTH: shader = Shader::Get("texture"); break;
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
	if (mr_texture) shader->setUniform("u_metallic_roughness_texture", mr_texture, 1);
	if (emissive_texture) shader->setUniform("u_emissive_texture", emissive_texture, 2);
	if (occ_texture) shader->setUniform("u_occ_texture", occ_texture, 3);
	if (normal_texture) shader->setUniform("u_normal_texture", normal_texture, 4);
	shader->setUniform("u_read_normal", have_normalmap);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	// light information
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_emissive_factor", material->emissive_factor);


	// SINGLE PASS
	if (render_mode == DEFAULT)
	{
		// lights
		int num_lights = scene->l_entities.size();
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
			LightEntity* lent = scene->l_entities[j];
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
		shader->setUniform3Array("u_direction", (float*)&light_vector, 5);
		shader->setUniform1("u_num_lights", num_lights);
		shader->setUniform1Array("u_maxdist", (float*)&max_distances, 5);
		shader->setUniform1Array("u_light_factor", (float*)&light_intensities, 5);
		shader->setUniform1Array("u_spotCosineCutoff", (float*)&light_cos_cutoff, 5);
		shader->setUniform1Array("u_spotExponent", (float*)&light_exponents, 5);

		//do the draw call that renders the mesh into the screen
		mesh->render(GL_TRIANGLES);
	}

	// MULTI PASS
	else if (render_mode == SHOW_MULTI)
	{
		//allow to render pixels that have the same depth as the one in the depth buffer
		glDepthFunc(GL_LEQUAL);

		//set blending mode to additive, this will collide with materials with blend...
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);

		for (int i = 0; i < scene->l_entities.size(); ++i)
		{
			LightEntity* light = scene->l_entities[i];
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
	glDepthFunc(GL_LESS);
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

void GTR::Renderer::joinGbuffers(GTR::Scene* scene, Camera* camera)
{
	float w = Application::instance->window_width;
	float h = Application::instance->window_height;
	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	//we need a fullscreen quad
	Mesh* quad = Mesh::getQuad();
	//Mesh* sphere = Mesh::Get("data/meshes/sphere.obj", true);

	//we need a shader specially for this task, lets call it "deferred"
	Shader* sh = Shader::Get("deferred");
	//Shader* sh = Shader::Get("deferred_ws");
	sh->enable();

	//pass the gbuffers to the shader
	sh->setUniform("u_color_texture", gbuffers_fbo.color_textures[0], 0);
	sh->setUniform("u_normal_texture", gbuffers_fbo.color_textures[1], 1);
	sh->setUniform("u_extra_texture", gbuffers_fbo.color_textures[2], 2);
	sh->setUniform("u_depth_texture", gbuffers_fbo.depth_texture, 3);

	//pass the inverse projection of the camera to reconstruct world pos.
	sh->setUniform("u_inverse_viewprojection", inv_vp);
	//pass the inverse window resolution, this may be useful
	sh->setUniform("u_iRes", Vector2(1.0 / (float)w, 1.0 / (float)h));

	//pass all the information about the light and ambient…
	sh->setUniform("u_ambient_light", scene->ambient_light);

	quad->render(GL_TRIANGLES);

	glDisable(GL_DEPTH_TEST);
	sh->disable();
	//glFrontFace(GL_CCW);
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

