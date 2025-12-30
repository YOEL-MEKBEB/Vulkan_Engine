#include "camera.h"
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

void Camera::update(){
  glm::mat4 cameraRotation = getRotationMatrix();
  position += glm::vec3(cameraRotation * glm::vec4(velocity * 0.5f, 0.f));
  const Uint8* state = SDL_GetKeyboardState(nullptr);

  velocity.z = (state[SDL_SCANCODE_S] - state[SDL_SCANCODE_W]) * 0.3;
  velocity.x = (state[SDL_SCANCODE_D] - state[SDL_SCANCODE_A]) * 0.3;
  velocity.y = (state[SDL_SCANCODE_E] - state[SDL_SCANCODE_Q]) * 0.3;

} 


void Camera::processSDLEvent(SDL_Event& e){
  // if (e.type == SDL_KEYDOWN) {
  //       if (e.key.keysym.sym == SDLK_w) { velocity.z = -0.3; }
  //       if (e.key.keysym.sym == SDLK_s) { velocity.z = 0.3; }
  //       if (e.key.keysym.sym == SDLK_a) { velocity.x = -0.3; }
  //       if (e.key.keysym.sym == SDLK_d) { velocity.x = 0.3; }
  //       if (e.key.keysym.sym == SDLK_e) { velocity.y = 0.3; }
  //       if (e.key.keysym.sym == SDLK_q) { velocity.y = -0.3; }
  //   }

  //   if (e.type == SDL_KEYUP) {
  //       if (e.key.keysym.sym == SDLK_w) { velocity.z = 0; }
  //       if (e.key.keysym.sym == SDLK_s) { velocity.z = 0; }
  //       if (e.key.keysym.sym == SDLK_a) { velocity.x = 0; }
  //       if (e.key.keysym.sym == SDLK_d) { velocity.x = 0; }
  //       if (e.key.keysym.sym == SDLK_e) { velocity.y = 0; }
  //       if (e.key.keysym.sym == SDLK_q) { velocity.y = 0; }
  //   }
  
    if (e.type == SDL_MOUSEBUTTONDOWN) {
        if (e.button.button == SDL_BUTTON_RIGHT) {
            // Right mouse button was clicked!
            rMouse = true;
            fmt::print("rMouse is true\n");
        }
    }
    if (e.type == SDL_MOUSEBUTTONUP) {
        if (e.button.button == SDL_BUTTON_RIGHT) {
            // Right mouse button was unclicked!
            rMouse = false;
            fmt::print("rMouse is false\n");
        }
    }
    

    if (e.type == SDL_MOUSEMOTION && rMouse) {
        yaw += (float)e.motion.xrel / 200.f;
        pitch -= (float)e.motion.yrel / 200.f;
    }

}

glm::mat4 Camera::getViewMatrix()
{
    // to create a correct model view, we need to move the world in opposite
    // direction to the camera
    //  so we will create the camera model matrix and invert
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
    glm::mat4 cameraRotation = getRotationMatrix();
    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getRotationMatrix()
{
    // fairly typical FPS style camera. we join the pitch and yaw rotations into
    // the final rotation matrix

    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3 { 1.f, 0.f, 0.f });
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3 { 0.f, -1.f, 0.f });

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}
