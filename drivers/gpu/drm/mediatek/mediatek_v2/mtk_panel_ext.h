/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#ifndef __MTK_PANEL_EXT_H__
#define __MTK_PANEL_EXT_H__

#include <drm/drm_panel.h>
#include <drm/drm_modes.h>
#include <drm/mediatek_drm.h>
#include <uapi/drm/mi_disp.h>

#define RT_MAX_NUM 10
#define ESD_CHECK_NUM 3
#define MAX_TX_CMD_NUM 25
#define MAX_RX_CMD_NUM 25
#define READ_DDIC_SLOT_NUM 4
#define MAX_DYN_CMD_NUM 20

#define MAX_TX_CMD_NUM_PACK 64

#ifdef CONFIG_MI_DISP
//#ifdef READ_DDIC_SLOT_NUM
//#undef READ_DDIC_SLOT_NUM
//#endif
//#define READ_DDIC_SLOT_NUM 12

#define PANEL_PWM_DEMURA_BACKLIGHT_THRESHOLD 451
#endif

struct mtk_dsi;
struct cmdq_pkt;
struct mtk_panel_para_table {
	u8 count;
	u8 para_list[64];
};

/*
 *	DSI data type:
 *	DSI_DCS_WRITE_SHORT_PACKET_NO_PARAM		0x05
 *	DSI_DCS_WRITE_SHORT_PACKET_1_PARAM		0x15
 *	DSI_DCS_WRITE_LONG_PACKET					0x39
 *	DSI_DCS_READ_NO_PARAM						0x06

 *	DSI_GERNERIC_WRITE_SHORT_NO_PARAM			0x03
 *	DSI_GERNERIC_WRITE_SHORT_1_PARAM			0x13
 *	DSI_GERNERIC_WRITE_SHORT_1_PARAM			0x23
 *	DSI_GERNERIC_WRITE_LONG_PACKET				0x29
 *	DSI_GERNERIC_READ_NO_PARAM					0x04
 *	DSI_GERNERIC_READ_1_PARAM					0x14
 *	DSI_GERNERIC_READ_2_PARAM					0x24
 */

/**
 * struct mtk_ddic_dsi_msg - MTK write/read DDIC RG cmd buffer
 * @channel: virtual channel id
 * @flags: flags controlling this message transmission
 * @type: payload data type array
 * @tx_len: length of @tx_buf
 * @tx_buf: data array to be written
 * @tx_cmd_num: tx cmd number
 * @rx_len: length of @rx_buf
 * @rx_buf: data array to be read, or NULL
 * @rx_cmd_num: rx cmd number
 */
struct mtk_ddic_dsi_msg {
	u8 channel;
	u16 flags;

	u8 type[MAX_TX_CMD_NUM];
	size_t tx_len[MAX_TX_CMD_NUM];
	const void *tx_buf[MAX_TX_CMD_NUM];
	size_t tx_cmd_num;

	size_t rx_len[MAX_RX_CMD_NUM];
	void *rx_buf[MAX_RX_CMD_NUM];
	size_t rx_cmd_num;
	u16 is_package;
	u16 is_hs;
};

struct DSI_RX_DATA_REG {
	unsigned char byte0;
	unsigned char byte1;
	unsigned char byte2;
	unsigned char byte3;
};

struct mtk_ddic_cmd {
	unsigned int cmd_num;
	unsigned char *para_list;
};

struct mtk_ddic_dsi_cmd {
	unsigned int is_package;
	unsigned int is_hs;
	unsigned int cmd_count;
	struct mtk_ddic_cmd mtk_ddic_cmd_table[MAX_TX_CMD_NUM_PACK];
};

typedef void (*dcs_write_gce) (struct mtk_dsi *dsi, struct cmdq_pkt *handle,
				const void *data, size_t len);
typedef void (*dcs_write_gce_pack) (struct mtk_dsi *dsi, struct cmdq_pkt *handle,
				struct mtk_ddic_dsi_cmd *para_table);
typedef void (*dcs_grp_write_gce) (struct mtk_dsi *dsi, struct cmdq_pkt *handle,
				struct mtk_panel_para_table *para_table,
				unsigned int para_size);
typedef int (*panel_tch_rst) (void);

enum MTK_PANEL_OUTPUT_PORT_MODE {
	MTK_PANEL_SINGLE_PORT = 0x0,
	MTK_PANEL_DSC_SINGLE_PORT,
	MTK_PANEL_DUAL_PORT,
};

enum MTK_PANEL_SPR_OUTPUT_MODE {
	MTK_PANEL_SPR_OUTPUT_MODE_NOT_DEFINED = 0,
	MTK_PANEL_PACKED_SPR_8_BITS = 1,
	MTK_PANEL_lOOSELY_SPR_8_BITS,
	MTK_PANEL_lOOSELY_SPR_10_BITS,
	MTK_PANEL_PACKED_SPR_12_BITS,
};

enum MTK_PANEL_SPR_MODE {
	MTK_PANEL_RGBG_BGRG_TYPE = 0,
	MTK_PANEL_BGRG_RGBG_TYPE,
	MTK_PANEL_RGBRGB_BGRBGR_TYPE,
	MTK_PANEL_BGRBGR_RGBRGB_TYPE,
	MTK_PANEL_RGBRGB_BRGBRG_TYPE,
	MTK_PANEL_BRGBRG_RGBRGB_TYPE,
	MTK_PANEL_EXT_TYPE,
};

struct esd_check_item {
	unsigned char cmd;
	unsigned char count;
	unsigned char para_list[RT_MAX_NUM];
	unsigned char mask_list[RT_MAX_NUM];
};

enum MTK_PANEL_MODE_SWITCH_STAGE {
	BEFORE_DSI_POWERDOWN,
	AFTER_DSI_POWERON,
};

enum MIPITX_PHY_PORT {
	MIPITX_PHY_PORT_0 = 0,
	MIPITX_PHY_PORT_1,
	MIPITX_PHY_PORT_NUM
};

enum MIPITX_PHY_LANE_SWAP {
	MIPITX_PHY_LANE_0 = 0,
	MIPITX_PHY_LANE_1,
	MIPITX_PHY_LANE_2,
	MIPITX_PHY_LANE_3,
	MIPITX_PHY_LANE_CK,
	MIPITX_PHY_LANE_RX,
	MIPITX_PHY_LANE_NUM
};

enum MODE_CHANGE_INDEX {
	MODE_NOT_DEFINED = 0,
	MODE_DSI_VFP = BIT(0),
	MODE_DSI_HFP = BIT(1),
	MODE_DSI_CLK = BIT(2),
	MODE_DSI_RES = BIT(3),
};

enum MTK_LCM_DUMP_FLAG {
	MTK_DRM_PANEL_DUMP_PARAMS,
	MTK_DRM_PANEL_DUMP_OPS,
	MTK_DRM_PANEL_DUMP_ALL
};

enum SPR_COLOR_PARAMS_TYPE {
	SPR_WEIGHT_SET = 0,
	SPR_BORDER_SET,
	SPR_SPE_SET,
	SPR_COLOR_PARAMS_TYPE_NUM,
};

struct spr_color_params {
	enum SPR_COLOR_PARAMS_TYPE spr_color_params_type;
	unsigned int count;
	unsigned char para_list[80];
	unsigned char tune_list[80];
};

struct mtk_panel_cm_params {
	unsigned int enable;
	unsigned int relay;
	unsigned int cm_c00;
	unsigned int cm_c01;
	unsigned int cm_c02;
	unsigned int cm_c10;
	unsigned int cm_c11;
	unsigned int cm_c12;
	unsigned int cm_c20;
	unsigned int cm_c21;
	unsigned int cm_c22;
	unsigned int cm_coeff_round_en;
	unsigned int cm_precision_mask;
	unsigned int bits_switch;
	unsigned int cm_gray_en;
};

struct mtk_panel_spr_params {
	unsigned int enable;
	unsigned int relay;
	unsigned int rgb_swap;
	unsigned int bypass_dither;
	unsigned int postalign_en;
	unsigned int wrap_mode;
	unsigned int specialcaseen;
	unsigned int indata_res_sel;
	unsigned int outdata_res_sel;
	unsigned int padding_repeat_en;
	unsigned int postalign_6type_mode_en;
	unsigned int custom_header_en;
	unsigned int custom_header;
	unsigned int spr_format_type;
	unsigned int rg_xy_swap;
	struct spr_color_params spr_color_params[SPR_COLOR_PARAMS_TYPE_NUM];

};

struct mtk_panel_dsc_params {
	unsigned int enable;
	unsigned int ver; /* [7:4] major [3:0] minor */
	unsigned int slice_mode;
	unsigned int rgb_swap;
	unsigned int dsc_cfg;
	unsigned int rct_on;
	unsigned int bit_per_channel;
	unsigned int dsc_line_buf_depth;
	unsigned int bp_enable;
	unsigned int bit_per_pixel;
	unsigned int pic_height; /* need to check */
	unsigned int pic_width;  /* need to check */
	unsigned int slice_height;
	unsigned int slice_width;
	unsigned int chunk_size;
	unsigned int xmit_delay;
	unsigned int dec_delay;
	unsigned int scale_value;
	unsigned int increment_interval;
	unsigned int decrement_interval;
	unsigned int line_bpg_offset;
	unsigned int nfl_bpg_offset;
	unsigned int slice_bpg_offset;
	unsigned int initial_offset;
	unsigned int final_offset;
	unsigned int flatness_minqp;
	unsigned int flatness_maxqp;
	unsigned int rc_model_size;
	unsigned int rc_edge_factor;
	unsigned int rc_quant_incr_limit0;
	unsigned int rc_quant_incr_limit1;
	unsigned int rc_tgt_offset_hi;
	unsigned int rc_tgt_offset_lo;
	unsigned int dsc_config_panel_name;
};
struct mtk_dsi_phy_timcon {
	unsigned int hs_trail;
	unsigned int hs_prpr;
	unsigned int hs_zero;
	unsigned int lpx;
	unsigned int ta_get;
	unsigned int ta_sure;
	unsigned int ta_go;
	unsigned int da_hs_exit;
	unsigned int clk_trail;
	unsigned int cont_det;
	unsigned int da_hs_sync;
	unsigned int clk_zero;
	unsigned int clk_hs_prpr;
	unsigned int clk_hs_exit;
	unsigned int clk_hs_post;
};

struct dynamic_mipi_params {
	unsigned int switch_en;
	unsigned int pll_clk;
	unsigned int data_rate;

	unsigned int vsa;
	unsigned int vbp;
	unsigned int vfp;
	unsigned int vfp_lp_dyn;

	unsigned int hsa;
	unsigned int hbp;
	unsigned int hfp;
	/*Msync 2.0*/
	unsigned int max_vfp_for_msync_dyn;
};

struct dfps_switch_cmd {
	unsigned int src_fps;
	unsigned int cmd_num;
	unsigned char para_list[64];
};

struct dynamic_fps_params {
	unsigned int switch_en;
	unsigned int vact_timing_fps;
	unsigned int data_rate;
	struct dfps_switch_cmd dfps_cmd_table[MAX_DYN_CMD_NUM];
};

/* M-SYNC2.0 */
#define MSYNC_MAX_CMD_NUM 20
#define MSYNC_MAX_LEVEL 20

enum TE_TYPE {
	NORMAL_TE = 0,
	REQUEST_TE = 1,
	MULTI_TE = 2,
	TRIGGER_LEVEL_TE = 4,
};

struct msync_cmd_list {
	unsigned int cmd_num;
	__u8 para_list[64];
};

struct msync_request_te_level {
	unsigned int id;
	unsigned int level_fps;
	unsigned int max_fps;
	unsigned int min_fps;
	struct msync_cmd_list cmd_list[MSYNC_MAX_CMD_NUM];
};

struct msync_request_te_table {
	unsigned char msync_ctrl_idx;
	unsigned char msync_rte_idx;
	unsigned char msync_valid_te_idx;
	unsigned char msync_max_vfp_idx;
	unsigned char msync_en_byte;
	unsigned char msync_en_mask;
	unsigned char delay_mode_byte;
	unsigned char delay_mode_mask;
	unsigned char valid_te_start_1_byte;
	unsigned char valid_te_start_1_mask;
	unsigned char valid_te_start_2_byte;
	unsigned char valid_te_start_2_mask;
	unsigned char valid_te_end_1_byte;
	unsigned char valid_te_end_1_mask;
	unsigned char valid_te_end_2_byte;
	unsigned char valid_te_end_2_mask;
	struct msync_cmd_list rte_cmd_list[MSYNC_MAX_CMD_NUM];
	struct msync_request_te_level request_te_level[MSYNC_MAX_LEVEL];
};

struct msync_multi_te_table {
	struct msync_level_table multi_te_level[MSYNC_MAX_LEVEL];
};

struct msync_trigger_level_te {
	unsigned int id;
	unsigned int level_fps;
	unsigned int max_fps;
	unsigned int min_fps;
	struct msync_cmd_list cmd_list[MSYNC_MAX_CMD_NUM];
};

struct msync_trigger_level_te_table {
	struct msync_trigger_level_te trigger_level_te_level[MSYNC_MAX_LEVEL];
};

struct msync_cmd_table {
	unsigned int te_type;
	unsigned int msync_max_fps;
	unsigned int msync_min_fps;
	unsigned int msync_level_num;
	unsigned int delay_frame_num;
	struct msync_request_te_table request_te_tb;
	struct msync_multi_te_table multi_te_tb;
	struct msync_trigger_level_te_table trigger_level_te_tb;
};

struct mtk_panel_params {
	unsigned int pll_clk;
	unsigned int data_rate;
	struct mtk_dsi_phy_timcon phy_timcon;
	unsigned int vfp_low_power;
	struct dynamic_mipi_params dyn;
	struct dynamic_fps_params dyn_fps;
	struct mtk_ddic_dsi_cmd send_cmd_to_ddic;
	unsigned int cust_esd_check;
	unsigned int esd_check_enable;
	struct esd_check_item lcm_esd_check_table[ESD_CHECK_NUM];
	unsigned int ssc_enable;
	unsigned int ssc_range;
	int lcm_color_mode;
	unsigned int min_luminance;
	unsigned int average_luminance;
	unsigned int max_luminance;
	unsigned int round_corner_en;
	unsigned int corner_pattern_height;
	unsigned int corner_pattern_height_bot;
	unsigned int corner_pattern_tp_size;
	unsigned int corner_pattern_tp_size_l;
	unsigned int corner_pattern_tp_size_r;
	void *corner_pattern_lt_addr;
	void *corner_pattern_lt_addr_l;
	void *corner_pattern_lt_addr_r;
	unsigned int physical_width_um;
	unsigned int physical_height_um;
	unsigned int lane_swap_en;
	unsigned int is_cphy;
	enum MIPITX_PHY_LANE_SWAP
		lane_swap[MIPITX_PHY_PORT_NUM][MIPITX_PHY_LANE_NUM];
	struct mtk_panel_dsc_params dsc_params;
	unsigned int output_mode;
	unsigned int spr_output_mode;
	unsigned int lcm_cmd_if;
	unsigned int hbm_en_time;
	unsigned int hbm_dis_time;
	unsigned int lcm_index;
	unsigned int wait_sof_before_dec_vfp;
	unsigned int doze_delay;
	unsigned int lp_perline_en; //0: lp perframe 1: lp perline
	unsigned int cmd_null_pkt_en;
	unsigned int cmd_null_pkt_len;
//Settings for LFR Function:
	unsigned int lfr_enable;
	unsigned int lfr_minimum_fps;
	/*Msync 2.0*/
	unsigned int msync2_enable;
	unsigned int max_vfp_for_msync;
	struct msync_cmd_table msync_cmd_table;

	struct mtk_panel_cm_params cm_params;
	struct mtk_panel_spr_params spr_params;
#ifdef CONFIG_MI_DISP
	int err_flag_irq_gpio;
	int err_flag_irq_flags;
#endif

#ifdef CONFIG_MI_DISP_FOD_SYNC
	/*block backlight until aod disapper*/
	int bl_sync_enable;
	/*delay to set doze_brightness*/
	int aod_delay_enable;
#endif
};

struct mtk_panel_ext {
	struct mtk_panel_funcs *funcs;
	struct mtk_panel_params *params;
};

struct mtk_panel_ctx {
	struct drm_panel *panel;
	struct mtk_panel_ext *ext;

	struct list_head list;
};

enum mtk_lcm_version {
	MTK_NULL_LCM_DRV,
	MTK_LEGACY_LCM_DRV,
	MTK_COMMON_LCM_DRV,
};

struct mtk_panel_funcs {
	int (*set_backlight_cmdq)(void *dsi_drv, dcs_write_gce cb,
		void *handle, unsigned int level);
	int (*set_aod_light_mode)(void *dsi_drv, dcs_write_gce cb,
		void *handle, unsigned int mode);
	int (*set_backlight_grp_cmdq)(void *dsi_drv, dcs_grp_write_gce cb,
		void *handle, unsigned int level);
	int (*reset)(struct drm_panel *panel, int on);
	int (*ata_check)(struct drm_panel *panel);
	int (*ext_param_set)(struct drm_panel *panel,
		struct drm_connector *connector, unsigned int mode);
	int (*ext_param_get)(struct drm_panel *panel,
		struct drm_connector *connector,
		struct mtk_panel_params **ext_para,
		unsigned int mode);
	int (*mode_switch)(struct drm_panel *panel,
		struct drm_connector *connector, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage);
	int (*mode_switch_hs)(struct drm_panel *panel, struct drm_connector *connector,
		void *dsi_drv, unsigned int cur_mode, unsigned int dst_mode,
		enum MTK_PANEL_MODE_SWITCH_STAGE stage, dcs_write_gce_pack cb);
	int (*msync_te_level_switch)(void *dsi, dcs_write_gce cb,
		void *handle, unsigned int fps_level);
	int (*msync_te_level_switch_grp)(void *dsi, dcs_grp_write_gce cb,
		void *handle, unsigned int fps_level);
	int (*msync_cmd_set_min_fps)(void *dsi, dcs_write_gce cb,
			void *handle, unsigned int flag);
	int (*get_virtual_heigh)(void);
	int (*get_virtual_width)(void);
	/**
	 * @doze_enable_start:
	 *
	 * Call the @doze_enable_start before starting AOD mode.
	 * The LCM off may add here to avoid panel show unexpected
	 * content when switching to specific panel low power mode.
	 */
	int (*doze_enable_start)(struct drm_panel *panel,
		void *dsi_drv, dcs_write_gce cb, void *handle);

	/**
	 * @doze_enable:
	 *
	 * Call the @doze_enable starts AOD mode.
	 */
	int (*doze_enable)(struct drm_panel *panel,
		void *dsi_drv, dcs_write_gce cb, void *handle);

	/**
	 * @doze_disable:
	 *
	 * Call the @doze_disable before ending AOD mode.
	 */
	int (*doze_disable)(struct drm_panel *panel,
		void *dsi_drv, dcs_write_gce cb, void *handle);

	/**
	 * @doze_post_disp_on:
	 *
	 * In some situation, the LCM off may set in @doze_enable & @disable.
	 * After LCM switch to the new mode stable, system call
	 * @doze_post_disp_on to turn on panel.
	 */
	int (*doze_post_disp_on)(struct drm_panel *panel,
		void *dsi_drv, dcs_write_gce cb, void *handle);

	/**
	 * @doze_area:
	 *
	 * Send the panel area in command here.
	 */
	int (*doze_area)(struct drm_panel *panel,
		void *dsi_drv, dcs_write_gce cb, void *handle);

	/**
	 * @doze_get_mode_flags:
	 *
	 * If CV switch is needed for doze mode, fill the mode_flags in this
	 * function for both CMD and VDO mode.
	 */
	unsigned long (*doze_get_mode_flags)(
		struct drm_panel *panel, int aod_en);

	int (*hbm_set_cmdq)(struct drm_panel *panel, void *dsi_drv,
			    dcs_write_gce cb, void *handle, bool en);
	void (*hbm_get_state)(struct drm_panel *panel, bool *state);
	void (*hbm_get_wait_state)(struct drm_panel *panel, bool *wait);
	bool (*hbm_set_wait_state)(struct drm_panel *panel, bool wait);

	void (*lcm_dump)(struct drm_panel *panel, enum MTK_LCM_DUMP_FLAG flag);
	enum mtk_lcm_version (*get_lcm_version)(void);
	int (*send_ddic_cmd_pack)(struct drm_panel *panel,
		void *dsi_drv, dcs_write_gce_pack cb, void *handle);
#ifdef CONFIG_MI_DISP
#ifdef CONFIG_MI_DISP_ESD_CHECK
	void (*esd_restore_backlight)(struct drm_panel *panel);
	int (*esd_check_read_prepare)(struct drm_panel *panel);
#else
	void (*esd_restore_backlight)(struct drm_panel *panel,
		void *dsi_drv, dcs_write_gce cb, void *handle);
#endif
	/* power-on for vddi */
	int (*panel_poweron)(struct drm_panel *panel);

	/* power-off for vddi */
	int (*panel_poweroff)(struct drm_panel *panel);

	bool (*get_panel_initialized)(struct drm_panel *panel);
	int (*get_panel_info)(struct drm_panel *panel, char *buf);
	int (*set_backlight_i2c)(struct drm_panel *panel, unsigned int level);
	int (*led_i2c_reg_op)(char *buffer, int op, int count);
	int (*hbm_fod_control)(struct drm_panel *panel, bool en);
	int (*set_lhbm_fod)(struct mtk_dsi *dsi, enum local_hbm_state);
	int (*normal_hbm_control)(struct drm_panel *panel, uint32_t level);
	int (*setbacklight_control)(struct drm_panel *panel, unsigned int level);
	int (*set_doze_brightness)(struct drm_panel *panel, int doze_brightness);
	int (*get_doze_brightness)(struct drm_panel *panel, u32 *doze_brightness);
	void (*aod_set_state)(struct drm_panel *panel, bool *state);
	void (*aod_get_state)(struct drm_panel *panel, bool *state);
	void (*set_nolp)(struct drm_panel *panel);
	void (*get_unset_doze_brightness)(struct drm_panel *panel, int *state);
	void (*panel_id_get)(struct drm_panel *panel);
	void (*panel_set_crc_srgb)(struct drm_panel *panel);
	void (*panel_set_crc_p3)(struct drm_panel *panel);
	void (*panel_set_crc_p3_d65)(struct drm_panel *panel);
	void (*panel_set_crc_p3_flat)(struct drm_panel *panel);
	void (*panel_set_crc_off)(struct drm_panel *panel);
	void (*panel_dimming_control)(struct drm_panel *panel, bool en);
	int (*panel_freq_switch)(struct drm_panel *panel, unsigned int cur_mode, unsigned int dst_mode);
	void (*hbm_need_delay)(struct drm_panel *panel, bool *state);
	void (*panel_elvss_control)(struct drm_panel *panel, bool en);
	void (*get_hbm_solution)(struct drm_panel *panel, int *solution);
	int (*get_panel_dynamic_fps)(struct drm_panel *panel, u32 *fps);
	int (*fps_switch_mode_set_cmdq)(struct drm_panel *panel, void *dsi_drv,
			    dcs_write_gce cb, void *handle);
	void (*panel_set_dc)(struct drm_panel *panel, bool enable);
	bool (*panel_get_dc)(struct drm_panel *panel);
	void (*set_dc_backlight)(struct drm_panel *panel, int brightness);
	int (*panel_pwm_demura_gain_update)(struct drm_panel *panel, int high_brightness);
	struct mtk_ddic_dsi_msg* (*get_esd_check_read_prepare_cmdmesg)(void);
	int (*get_panel_max_brightness_clone)(struct drm_panel *panel, u32 *max_brightness_clone);
	int (*get_panel_factory_max_brightness)(struct drm_panel *panel, u32 *max_brightness_clone);
	int (*panel_set_gir_on)(struct drm_panel *panel);
	int (*panel_set_gir_off)(struct drm_panel *panel);
	int (*panel_get_gir_status)(struct drm_panel *panel);
	void (*panel_set_bist_enable)(struct drm_panel *panel, bool enable);
	void (*panel_set_bist_color)(struct drm_panel *panel, u8 *rgb);
	void (*panel_set_round_enable)(struct drm_panel *panel, bool enable);
	void (*panel_set_gir_on_read_B8reg)(void);
	void (*panel_set_dc_lut_params)(struct drm_panel *panel, char *exitDClut60, char *enterDClut60, char *exitDClut120, char *enterDClut120, int count);
	void (*init)(struct drm_panel *panel);
	int (*trigger_get_wpinfo)(struct drm_panel *panel, char *buf, size_t size);
	int (*get_wp_info)(struct drm_panel *panel, char *buf, size_t size);
	int (*get_grayscale_info)(struct drm_panel *panel, char *buf, size_t size);
	int (*set_spr_status)(struct drm_panel *panel, int status);
	int (*panel_set_dc_crc)(struct drm_panel *panel, int hw_brightness_evel, int crc_coef0, int crc_coef1);
	int (*panel_set_dc_crc_bl_pack)(struct drm_panel *panel, int hw_brightness_evel, int crc_coef0, int crc_coef1);
	int (*panel_set_dc_crc_off)(struct drm_panel *panel);
	int (*panel_restore_crc_level)(struct drm_panel *panel, bool need_lock);
	void (*set_dc_threshold)(struct drm_panel *panel, int dc_threshold);
	int (*panel_fod_lhbm_init)(struct mtk_dsi * dsi);
	int (*doze_suspend)(struct drm_panel *panel, void *dsi_drv, dcs_write_gce cb, void *handle);
	int (*fod_state_check)(void *dsi_drv, dcs_write_gce cb, void *handle);
#endif
};

void mtk_panel_init(struct mtk_panel_ctx *ctx);
void mtk_panel_add(struct mtk_panel_ctx *ctx);
void mtk_panel_remove(struct mtk_panel_ctx *ctx);
int mtk_panel_attach(struct mtk_panel_ctx *ctx, struct drm_panel *panel);
int mtk_panel_detach(struct mtk_panel_ctx *ctx);
struct mtk_panel_ext *find_panel_ext(struct drm_panel *panel);
struct mtk_panel_ctx *find_panel_ctx(struct drm_panel *panel);
int mtk_panel_ext_create(struct device *dev,
			 struct mtk_panel_params *ext_params,
			 struct mtk_panel_funcs *ext_funcs,
			 struct drm_panel *panel);
int mtk_panel_tch_handle_reg(struct drm_panel *panel);
void **mtk_panel_tch_handle_init(void);
int mtk_panel_tch_rst(struct drm_panel *panel);
enum mtk_lcm_version mtk_drm_get_lcm_version(void);
#endif
