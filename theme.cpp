#include "theme.h"
#include "director.h"
#include "util.h"
#include <iostream>
#include <trance.pb.h>
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>

Theme::Theme(const trance_pb::Theme& proto)
: _image_paths{proto.image_path()}
, _animation_paths{proto.animation_path()}
, _font_paths{proto.font_path()}
, _text_lines{proto.text_line()}
, _target_load{0}
{
}

// TODO: get rid of this somehow. Copying needs mutexes, technically.
Theme::Theme(const Theme& theme)
: _image_paths{theme._image_paths}
, _animation_paths{theme._animation_paths}
, _font_paths{theme._font_paths}
, _text_lines{theme._text_lines}
, _target_load(theme._target_load)
{
}

Image Theme::get_image() const
{
  // Lock the mutex so we don't interfere with the thread calling
  // ThemeBank::async_update().
  _image_mutex.lock();
  if (_images.empty()) {
    _image_mutex.unlock();
    return get_animation(random(2 << 16));
  }
  std::size_t index = _image_paths.next_index(false);
  auto it = _images.find(index);
  it->second.ensure_texture_uploaded();
  Image image = it->second;
  _image_mutex.unlock();
  return image;
}

Image Theme::get_animation(std::size_t frame) const
{
  _animation_mutex.lock();
  if (_animation_images.empty()) {
    _animation_mutex.unlock();
    return {};
  }
  auto len = _animation_images.size();
  auto f = frame % (2 * len - 2);
  f = f < len ? f : 2 * len - 2 - f;
  _animation_images[f].ensure_texture_uploaded();
  Image image = _animation_images[f];
  _animation_mutex.unlock();
  return image;
}

const std::string& Theme::get_text() const
{
  return _text_lines.next();
}

const std::string& Theme::get_font() const
{
  return _font_paths.next();
}

void Theme::set_target_load(std::size_t target_load)
{
  _target_load = target_load;
}

void Theme::perform_swap()
{
  if (_animation_paths.size() > 2 && random_chance(4)) {
    load_animation_internal();
    return;
  }
  // Swap if there's definitely an image loaded beyond the one currently
  // displayed.
  if (_images.size() > 2 && _image_paths.enabled_count()) {
    unload_image_internal();
    load_image_internal();
  }
}

void Theme::perform_load()
{
  if (!_animation_paths.empty()) {
    if (_target_load && _animation_images.empty()) {
      load_animation_internal();
    }
    else if (!_target_load && !_animation_images.empty()) {
      unload_animation_internal();
    }
  }

  if (_images.size() < _target_load && _image_paths.enabled_count()) {
    load_image_internal();
  }
  else if (_images.size() > _target_load) {
    unload_image_internal();
  }
}

void Theme::perform_all_loads()
{
  while (!all_loaded()) {
    perform_load();
  }
}

bool Theme::all_loaded() const
{
  return (_images.size() == _target_load || !_image_paths.enabled_count()) &&
      (_animation_images.empty() == !_target_load || _animation_paths.empty());
}

std::size_t Theme::loaded() const
{
  return _images.size();
}

void Theme::load_image_internal()
{
  // Take a random still-enabled image, disable it and load the image.
  _image_mutex.lock();
  std::size_t index = _image_paths.next_index(true);
  _image_mutex.unlock();

  auto path = _image_paths.get(index);
  Image image = load_image(path);
  if (image) {
    _image_mutex.lock();
    _image_paths.set_enabled(index, false);
    _images.emplace(index, image);
    _image_mutex.unlock();
  }
}

void Theme::unload_image_internal()
{
  // Opposite of load_internal(): pick a disabled image at random, unload it,
  // and re-enable it.
  _image_mutex.lock();
  std::size_t index = _image_paths.next_index(false);
  _images.erase(index);
  _image_paths.set_enabled(index, true);
  _image_mutex.unlock();
}

void Theme::load_animation_internal()
{
  std::vector<Image> images = load_animation(_animation_paths.next());
  if (images.empty()) {
    return;
  }

  _animation_mutex.lock();
  std::swap(images, _animation_images);
  _animation_mutex.unlock();
  images.clear();
}

void Theme::unload_animation_internal()
{
  _animation_mutex.lock();
  _animation_images.clear();
  _animation_mutex.unlock();
}

ThemeBank::ThemeBank(const std::vector<trance_pb::Theme>& themes,
                     const trance_pb::SystemConfiguration& system)
: _image_cache_size{system.image_cache_size()}
, _updates{0}
, _cooldown{switch_cooldown}
{
  for (const auto& theme : themes) {
    _themes.emplace_back(theme);
  }
  if (themes.empty()) {
    _themes.push_back(Theme({}));
  }

  if (_themes.size() == 1) {
    // Always have at least two themes.
    Theme copy = _themes.back();
    _themes.emplace_back(copy);
  }
  if (_themes.size() == 2) {
    // Two active themes and switching just swaps them.
    _a = 0;
    _b = 1;
    _themes[0].set_target_load(_image_cache_size / 2);
    _themes[1].set_target_load(_image_cache_size / 2);
    _themes[0].perform_all_loads();
    _themes[1].perform_all_loads();
    return;
  }

  // For three themes, we keep every theme loaded at all times but swap the two
  // active ones.
  //
  // Four four or more themes, we have:
  // - 2 active themes (_a, _b)
  // - 1 loading in (_next)
  // - 1 being unloaded (_prev)
  // - some others
  _a = random(_themes.size());
  _b = random_excluding(_themes.size(), _a);
  do {
    _next = random_excluding(_themes.size(), _a);
  }
  while (_next == _b);

  _themes[_a].set_target_load(_image_cache_size / 3);
  _themes[_b].set_target_load(_image_cache_size / 3);
  _themes[_next].set_target_load(_image_cache_size / 3);
  _themes[_a].perform_all_loads();
  _themes[_b].perform_all_loads();

  if (_themes.size() == 3) {
    _themes[_next].perform_all_loads();
  }
  else {
    // _prev just needs to be some unused index.
    _prev = 0;
    while (_prev == _a || _prev == _b || _prev == _next) {
      ++_prev;
    }
  }
}

const Theme& ThemeBank::get(bool alternate) const
{
  return alternate ? _themes[_a] : _themes[_b];
}

void ThemeBank::maybe_upload_next()
{
  if (_themes.size() > 3 && _themes[_next].loaded() > 0) {
    _themes[_next].get_image();
  }
}

bool ThemeBank::change_themes()
{
  _cooldown = switch_cooldown;
  if (_themes.size() < 3) {
    // Only indexes need to be swapped.
    std::swap(_a, _b);
    return true;
  }
  if (_themes.size() == 3) {
    // Indexes need to be cycled.
    std::size_t t = _a;
    _a = _b;
    _b = _next;
    _next = _a;
    return true;
  }

  // For four or more themes, we need to wait until the next one has loaded in
  // sufficiently.
  if (!_themes[_prev].all_loaded() || !_themes[_next].all_loaded()) {
    return false;
  }

  _prev = _a;
  _a = _b;
  _b = _next;
  do {
    _next = random_excluding(_themes.size(), _prev);
  }
  while (_next == _a || _next == _b);

  // Update target loads.
  _themes[_prev].set_target_load(0);
  _themes[_next].set_target_load(_image_cache_size / 3);
  return true;
}

void ThemeBank::async_update()
{
  if (_cooldown) {
    --_cooldown;
    return;
  }

  ++_updates;
  // Swap some images from the active themes in and out every so often.
  if (_updates > 128) {
    _themes[_a].perform_swap();
    _themes[_b].perform_swap();
    _updates = 0;
  }
  if (_themes.size() == 3) {
    _themes[_next].perform_swap();
  }
  else if (_themes.size() >= 4) {
    _themes[_prev].perform_load();
    _themes[_next].perform_load();
  }
}