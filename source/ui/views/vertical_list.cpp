#include "ui/ui_common.hpp"
#include "ui/views/vertical_list.hpp"
#include "ui/views/specialized/succinct_video.hpp"
#include "ui/views/specialized/succinct_channel.hpp"
#include <vector>
#include <set>
#include <utility>

void VerticalListView::recursive_delete_subviews() {
	if (do_thumbnail_update) {
		for (auto view : views) {
			auto *cur_view = dynamic_cast<SuccinctVideoView *>(view);
			if (cur_view) thumbnail_cancel_request(cur_view->thumbnail_handle), cur_view->thumbnail_handle = -1;
			else {
				auto *cur_view = dynamic_cast<SuccinctChannelView *>(view);
				if (cur_view) thumbnail_cancel_request(cur_view->thumbnail_handle), cur_view->thumbnail_handle = -1;
			}
		}
		thumbnail_loaded_l = 0;
		thumbnail_loaded_r = 0;
	}
	for (auto view : views) {
		view->recursive_delete_subviews();
		delete view;
	}
	views.clear();
}
void VerticalListView::draw_() const {
	if (!draw_order.size()) {
		double y_offset = y0;
		for (auto view : views) {
			double y_bottom = y_offset + view->get_height();
			if (y_bottom >= 0 && y_offset < 240) view->draw(x0, y_offset);
			y_offset = y_bottom + margin;
		}
	} else {
		std::vector<float> y_pos(views.size() + 1, y0);
		for (size_t i = 0; i < views.size(); i++) y_pos[i + 1] = y_pos[i] + views[i]->get_height() + margin;
		for (auto i : draw_order) if (y_pos[i + 1] >= 0 && y_pos[i] < 240) views[i]->draw(x0, y_pos[i]);
	}
}
void VerticalListView::update_(Hid_info key) {
	double y_offset = y0;
	for (auto view : views) {
		double y_bottom = y_offset + view->get_height();
		if (y_bottom >= 0 && y_offset < 240) view->update(key, x0, y_offset);
		y_offset = y_bottom + margin;
	}
	if (do_thumbnail_update) {
		int video_num = views.size();
		int displayed_l = views.size();
		int displayed_r = -1;
		{
			int cur_y = y0;
			for (int i = 0; i < (int) views.size(); i++) {
				if (cur_y < 240) displayed_r = i;
				cur_y += views[i]->get_height();
				if (cur_y >= 0) displayed_l = std::min(displayed_l, i);
				cur_y += margin;
			}
			if (displayed_l > displayed_r) displayed_l = displayed_r = 0;
			else displayed_r++;
		}
		int request_target_l = std::max(0, displayed_l - (thumbnail_max_request - (displayed_r - displayed_l)) / 2);
		int request_target_r = std::min(video_num, request_target_l + thumbnail_max_request);
		// transition from [thumbnail_loaded_l, thumbnail_loaded_r) to [request_target_l, request_target_r)
		std::set<int> new_indexes, cancelling_indexes;
		for (int i = thumbnail_loaded_l; i < thumbnail_loaded_r; i++) cancelling_indexes.insert(i);
		for (int i = request_target_l; i < request_target_r; i++) new_indexes.insert(i);
		for (int i = thumbnail_loaded_l; i < thumbnail_loaded_r; i++) new_indexes.erase(i);
		for (int i = request_target_l; i < request_target_r; i++) cancelling_indexes.erase(i);
		
		for (auto i : cancelling_indexes) {
			auto *cur_view = dynamic_cast<SuccinctVideoView *>(views[i]);
			if (cur_view) {
				thumbnail_cancel_request(cur_view->thumbnail_handle);
				cur_view->thumbnail_handle = -1;
			} else {
				auto *cur_view = dynamic_cast<SuccinctChannelView *>(views[i]);
				if (cur_view) {
					thumbnail_cancel_request(cur_view->thumbnail_handle);
					cur_view->thumbnail_handle = -1;
				}
			}
		}
		for (auto i : new_indexes) {
			auto *cur_view = dynamic_cast<SuccinctVideoView *>(views[i]);
			if (cur_view) cur_view->thumbnail_handle = thumbnail_request(cur_view->thumbnail_url, thumbnail_scene, 0, ThumbnailType::VIDEO_THUMBNAIL);
			else {
				auto *cur_view = dynamic_cast<SuccinctChannelView *>(views[i]);
				if (cur_view) cur_view->thumbnail_handle = thumbnail_request(cur_view->thumbnail_url, thumbnail_scene, 0, ThumbnailType::ICON);
			}
		}
		
		thumbnail_loaded_l = request_target_l;
		thumbnail_loaded_r = request_target_r;
		
		std::vector<std::pair<int, int> > priority_list(request_target_r - request_target_l);
		auto dist = [&] (int i) { return i < displayed_l ? displayed_l - i : i - displayed_r + 1; };
		for (int i = request_target_l; i < request_target_r; i++) {
			auto *cur_view = dynamic_cast<SuccinctVideoView *>(views[i]);
			if (cur_view) priority_list[i - request_target_l] = {cur_view->thumbnail_handle, 500 - dist(i)};
			else {
				auto *cur_view = dynamic_cast<SuccinctChannelView *>(views[i]);
				if (cur_view) priority_list[i - request_target_l] = {cur_view->thumbnail_handle, 500 - dist(i)};
				else priority_list[i - request_target_l] = {-1, -1};
			}
		}
		thumbnail_set_priorities(priority_list);
	}
}
