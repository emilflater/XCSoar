/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>
	Tobias Bieniek <tobias.bieniek@gmx.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "MapWindow.hpp"
#include "Screen/Graphics.hpp"
#include "Screen/WindowCanvas.hpp"
#include "Screen/Layout.hpp"
#include "TopologyStore.h"
#include "RasterTerrain.h"
#include "TerrainRenderer.h"
#include "RasterWeather.h"
#include "Gauge/GaugeCDI.hpp"
#include "TaskClientUI.hpp"

#include <tchar.h>

// Initialization

ScreenGraphics MapGfx;

/**
 * Constructor of the MapWindow class
 */
MapWindow::MapWindow()
  :MapWindowProjection(),
   topology(NULL), terrain(NULL), weather(NULL),
   topology_dirty(true), terrain_dirty(true), weather_dirty(true),
   terrain_renderer(NULL),
   m_airspace(NULL), task(NULL),
   marks(NULL), 
   cdi(NULL),
   TargetDrag_State(0),
   BigZoom(true)
{
  TargetDrag_Location.Latitude = Angle();
  TargetDrag_Location.Longitude = Angle();
  zoomclimb.CruiseMapScale = 10;
  zoomclimb.ClimbMapScale = 0.25;
  zoomclimb.last_isclimb = false;
  zoomclimb.last_targetpan = false;
}

/**
 * Destructor of the MapWindow class
 */
MapWindow::~MapWindow()
{
  if (cdi != NULL)
    delete cdi;
  if (terrain_renderer != NULL)
    delete terrain_renderer;
}

void
MapWindow::set(ContainerWindow &parent, 
               const RECT _MapRectSmall,
               const RECT _MapRectBig)
{
  MapRectSmall = _MapRectSmall;
  MapRect = MapRectBig = _MapRectBig;

  WindowStyle style;
  style.enable_double_clicks();
  PaintWindow::set(parent, _T("XCSoarMap"), MapRect.left, MapRect.top,
                   MapRect.right - MapRect.left, MapRect.bottom - MapRect.top,
                   style);

  // initialize other systems
  InitialiseScaleList(SettingsMap());

  cdi = new GaugeCDI(parent); /* XXX better attach to "this"? */
}

/**
 * Copies the given basic and calculated info to the MapWindowBlackboard
 * and reads the Settings from the DeviceBlackboard.
 * @param nmea_info Basic info
 * @param derived_info Calculated info
 * @param settings_computer Computer settings to exchange
 * @param settings_map Map settings to exchange
 */
void
MapWindow::ReadBlackboard(const NMEA_INFO &nmea_info,
                          const DERIVED_INFO &derived_info,
                          const SETTINGS_COMPUTER &settings_computer,
                          const SETTINGS_MAP &settings_map)
{
  MapWindowBlackboard::ReadBlackboard(nmea_info, derived_info);
  ReadSettingsComputer(settings_computer);
  ReadSettingsMap(settings_map);
}

void
MapWindow::UpdateProjection()
{
  ApplyScreenSize();
  MapWindowProjection::ExchangeBlackboard(Calculated(), SettingsMap());
}

void
MapWindow::UpdateTopologyCache()
{
  if (topology != NULL)
    topology->TriggerUpdateCaches(*this);
}

void
MapWindow::UpdateTopology(bool force)
{
  if (!topology_dirty)
    return;

  topology_dirty = topology != NULL && SettingsMap().EnableTopology &&
    topology->ScanVisibility(*this, *getSmartBounds(), force);
}

void
MapWindow::UpdateTerrain()
{
  if (!terrain_dirty)
    return;

  // always service terrain even if it's not used by the map,
  // because it's used by other calculations
  if (terrain != NULL) {
    terrain->ServiceTerrainCenter(Basic().Location);
    terrain->ServiceCache();
  }

  terrain_dirty = false;
}

void
MapWindow::UpdateWeather()
{
  if (!weather_dirty)
    return;

  // always service weather even if it's not used by the map,
  // because it's potentially used by other calculations

  if (weather != NULL)
    weather->SetViewCenter(Basic().Location);

  weather_dirty = false;
}

/**
 * Handles the drawing of the moving map and is called by the DrawThread
 */
void
MapWindow::DrawThreadLoop(void)
{
  // Start the drawing timer (for drawing time calculation)
  StartTimer();

  //  static PeriodClock mclock;
  //  printf("draw %d\n",mclock.elapsed());
  //  mclock.update();

  if (BigZoom) {
    // quickly draw zoom level on top
    WindowCanvas canvas(*this);
    DrawMapScale(canvas, MapRect, true);
  }

  // Render the moving map
  Render(draw_canvas, MapRect);

  // Copy the rendered map to the drawing canvas
  invalidate();

  // Stop the drawing timer and calculate drawing time
  StopTimer();
}

bool
MapWindow::register_class(HINSTANCE hInstance)
{
#ifdef ENABLE_SDL
  return true;
#else /* !ENABLE_SDL */
  WNDCLASS wc;

  wc.hInstance = hInstance;
  wc.style = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
  wc.lpfnWndProc = MapWindow::WndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0 ;
  wc.hIcon = (HICON)NULL;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)GetStockObject (WHITE_BRUSH);
  wc.lpszMenuName = 0;
  wc.lpszClassName = _T("XCSoarMap");

  return (RegisterClass(&wc)!= FALSE);
#endif /* !ENABLE_SDL */
}

#ifdef WIN32
bool
MapWindow::identify(HWND hWnd)
{
  TCHAR name[16];
  if (::GetClassName(hWnd, name, sizeof(name)) == 0)
    return false;
  return _tcscmp(name, _T("XCSoarMap"));
}
#endif /* WIN32 */

void
MapWindow::set_topology(TopologyStore *_topology)
{
  topology = _topology;
}

void
MapWindow::set_terrain(RasterTerrain *_terrain)
{
  terrain = _terrain;

  if (terrain_renderer != NULL) {
    delete terrain_renderer;
    terrain_renderer = NULL;
  }
}

void
MapWindow::set_weather(RasterWeather *_weather)
{
  weather = _weather;

  if (terrain_renderer != NULL) {
    delete terrain_renderer;
    terrain_renderer = NULL;
  }
}

bool MapWindow::checkLabelBlock(const RECT brect) {
  return label_block.check(brect);
}


void MapWindow::SwitchZoomClimb(void) {

  bool isclimb = (DisplayMode == dmCircling);

  bool my_target_pan = SettingsMap().TargetPan;

  if (my_target_pan != zoomclimb.last_targetpan) {
    if (my_target_pan) {
      // save starting values
      if (isclimb) {
        zoomclimb.ClimbMapScale = GetMapScaleUser();
      } else {
        zoomclimb.CruiseMapScale = GetMapScaleUser();
      }
    } else {
      // restore scales
      if (isclimb) {
        RequestMapScale(zoomclimb.ClimbMapScale, SettingsMap());
      } else {
        RequestMapScale(zoomclimb.CruiseMapScale, SettingsMap());
      }
      BigZoom = true;
    }
    zoomclimb.last_targetpan = my_target_pan;
    return;
  }

  if (!my_target_pan && SettingsMap().CircleZoom) {
    if (isclimb != zoomclimb.last_isclimb) {
      if (isclimb) {
        // save cruise scale
        zoomclimb.CruiseMapScale = GetMapScaleUser();
        // switch to climb scale
        RequestMapScale(zoomclimb.ClimbMapScale, SettingsMap());
      } else {
        // leaving climb
        // save cruise scale
        zoomclimb.ClimbMapScale = GetMapScaleUser();
        RequestMapScale(zoomclimb.CruiseMapScale, SettingsMap());
        // switch to climb scale
      }
      BigZoom = true;
      zoomclimb.last_isclimb = isclimb;
    }
  }
}


void MapWindow::ApplyScreenSize() {
  if (SettingsMap().FullScreen) {
    SetMapRect(MapRectBig);
  } else {
    SetMapRect(MapRectSmall);
  }

  DisplayMode_t lastDisplayMode = DisplayMode;
  switch (SettingsMap().UserForceDisplayMode) {
  case dmCircling:
    DisplayMode = dmCircling;
    break;
  case dmCruise:
    DisplayMode = dmCruise;
    break;
  case dmFinalGlide:
    DisplayMode = dmFinalGlide;
    break;
  case dmNone:
    if (Calculated().Circling){
      DisplayMode = dmCircling;
    } else if (Calculated().task_stats.flight_mode_final_glide){
      DisplayMode = dmFinalGlide;
    } else
      DisplayMode = dmCruise;
    break;
  }
  if (lastDisplayMode != DisplayMode){
    SwitchZoomClimb();
  }
}

void
MapWindow::draw_bitmap(Canvas &canvas, const Bitmap &bitmap,
                       const int x, const int y,
                       const unsigned src_x_offset,
                       const unsigned src_y_offset,
                       const unsigned src_width,
                       const unsigned src_height,
                       bool centered)
{
  int x_offset = centered? IBLSCALE(src_width)/2 : 0;
  int y_offset = centered? IBLSCALE(src_height)/2 : 0;

  bitmap_canvas.background_opaque();
  bitmap_canvas.set_text_color(Color::WHITE);
  bitmap_canvas.select(bitmap);
  canvas.scale_copy(x-x_offset, y-y_offset,
		    bitmap_canvas,
		    src_x_offset, src_y_offset,
		    src_width, src_height);
}

void
MapWindow::draw_masked_bitmap(Canvas &canvas, const Bitmap &bitmap,
                              const int x, const int y,
                              const unsigned src_width,
                              const unsigned src_height,
                              bool centered)
{
  int x_offset = centered? IBLSCALE(src_width)/2 : 0;
  int y_offset = centered? IBLSCALE(src_height)/2 : 0;

  bitmap_canvas.background_opaque();
  bitmap_canvas.set_text_color(Color::WHITE);
  bitmap_canvas.select(bitmap);
  canvas.scale_or_and(x-x_offset, y-y_offset, bitmap_canvas, src_width, src_height);
}

bool MapWindow::draw_masked_bitmap_if_visible(Canvas &canvas,
					      Bitmap &bitmap,
					      const GEOPOINT &loc,
					      unsigned width,
					      unsigned height,
					      POINT *scin)
{
  POINT sc;
  POINT *scp = (scin!=NULL)? (scin):(&sc);

  if (LonLat2ScreenIfVisible(loc, scp)) {
    draw_masked_bitmap(canvas, bitmap, scp->x, scp->y, width, height, true);
    return true;
  }
  return false;
}


GlidePolar 
MapWindow::get_glide_polar() const
{
  return task != NULL ? task->get_glide_polar()
    : GlidePolar(fixed_zero);
}
