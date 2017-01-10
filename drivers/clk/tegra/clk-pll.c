/*
 * Copyright (c) 2012, 2013, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/clk.h>

#include "clk.h"

#define PLL_BASE_BYPASS BIT(31)
#define PLL_BASE_ENABLE BIT(30)
#define PLL_BASE_REF_ENABLE BIT(29)
#define PLL_BASE_OVERRIDE BIT(28)

#define PLL_BASE_DIVP_SHIFT 20
#define PLL_BASE_DIVP_WIDTH 3
#define PLL_BASE_DIVN_SHIFT 8
#define PLL_BASE_DIVN_WIDTH 10
#define PLL_BASE_DIVM_SHIFT 0
#define PLL_BASE_DIVM_WIDTH 5
#define PLLU_POST_DIVP_MASK 0x1

#define PLL_MISC_DCCON_SHIFT 20
#define PLL_MISC_CPCON_SHIFT 8
#define PLL_MISC_CPCON_WIDTH 4
#define PLL_MISC_CPCON_MASK ((1 << PLL_MISC_CPCON_WIDTH) - 1)
#define PLL_MISC_LFCON_SHIFT 4
#define PLL_MISC_LFCON_WIDTH 4
#define PLL_MISC_LFCON_MASK ((1 << PLL_MISC_LFCON_WIDTH) - 1)
#define PLL_MISC_VCOCON_SHIFT 0
#define PLL_MISC_VCOCON_WIDTH 4
#define PLL_MISC_VCOCON_MASK ((1 << PLL_MISC_VCOCON_WIDTH) - 1)

#define OUT_OF_TABLE_CPCON 8

#define PMC_PLLP_WB0_OVERRIDE 0xf8
#define PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE BIT(12)
#define PMC_PLLP_WB0_OVERRIDE_PLLM_OVERRIDE BIT(11)

#define PLL_POST_LOCK_DELAY 50

#define PLLDU_LFCON_SET_DIVN 600

#define PLLE_BASE_DIVCML_SHIFT 24
#define PLLE_BASE_DIVCML_WIDTH 4
#define PLLE_BASE_DIVP_SHIFT 16
#define PLLE_BASE_DIVP_WIDTH 7
#define PLLE_BASE_DIVN_SHIFT 8
#define PLLE_BASE_DIVN_WIDTH 8
#define PLLE_BASE_DIVM_SHIFT 0
#define PLLE_BASE_DIVM_WIDTH 8

#define PLLE_MISC_SETUP_BASE_SHIFT 16
#define PLLE_MISC_SETUP_BASE_MASK (0xffff << PLLE_MISC_SETUP_BASE_SHIFT)
#define PLLE_MISC_LOCK_ENABLE BIT(9)
#define PLLE_MISC_READY BIT(15)
#define PLLE_MISC_SETUP_EX_SHIFT 2
#define PLLE_MISC_SETUP_EX_MASK (3 << PLLE_MISC_SETUP_EX_SHIFT)
#define PLLE_MISC_SETUP_MASK (PLLE_MISC_SETUP_BASE_MASK |	\
			      PLLE_MISC_SETUP_EX_MASK)
#define PLLE_MISC_SETUP_VALUE (7 << PLLE_MISC_SETUP_BASE_SHIFT)

#define PLLE_SS_CTRL 0x68
#define PLLE_SS_CNTL_BYPASS_SS BIT(10)
#define PLLE_SS_CNTL_INTERP_RESET BIT(11)
#define PLLE_SS_CNTL_SSC_BYP BIT(12)
#define PLLE_SS_CNTL_CENTER BIT(14)
#define PLLE_SS_CNTL_INVERT BIT(15)
#define PLLE_SS_DISABLE (PLLE_SS_CNTL_BYPASS_SS | PLLE_SS_CNTL_INTERP_RESET |\
				PLLE_SS_CNTL_SSC_BYP)
#define PLLE_SS_MAX_MASK 0x1ff
#define PLLE_SS_MAX_VAL 0x25
#define PLLE_SS_INC_MASK (0xff << 16)
#define PLLE_SS_INC_VAL (0x1 << 16)
#define PLLE_SS_INCINTRV_MASK (0x3f << 24)
#define PLLE_SS_INCINTRV_VAL (0x20 << 24)
#define PLLE_SS_COEFFICIENTS_MASK \
	(PLLE_SS_MAX_MASK | PLLE_SS_INC_MASK | PLLE_SS_INCINTRV_MASK)
#define PLLE_SS_COEFFICIENTS_VAL \
	(PLLE_SS_MAX_VAL | PLLE_SS_INC_VAL | PLLE_SS_INCINTRV_VAL)

#define PLLE_AUX_PLLP_SEL	BIT(2)
#define PLLE_AUX_ENABLE_SWCTL	BIT(4)
#define PLLE_AUX_SEQ_ENABLE	BIT(24)
#define PLLE_AUX_PLLRE_SEL	BIT(28)

#define PLLE_MISC_PLLE_PTS	BIT(8)
#define PLLE_MISC_IDDQ_SW_VALUE	BIT(13)
#define PLLE_MISC_IDDQ_SW_CTRL	BIT(14)
#define PLLE_MISC_VREG_BG_CTRL_SHIFT	4
#define PLLE_MISC_VREG_BG_CTRL_MASK	(3 << PLLE_MISC_VREG_BG_CTRL_SHIFT)
#define PLLE_MISC_VREG_CTRL_SHIFT	2
#define PLLE_MISC_VREG_CTRL_MASK	(2 << PLLE_MISC_VREG_CTRL_SHIFT)

#define PLLCX_MISC_STROBE	BIT(31)
#define PLLCX_MISC_RESET	BIT(30)
#define PLLCX_MISC_SDM_DIV_SHIFT 28
#define PLLCX_MISC_SDM_DIV_MASK (0x3 << PLLCX_MISC_SDM_DIV_SHIFT)
#define PLLCX_MISC_FILT_DIV_SHIFT 26
#define PLLCX_MISC_FILT_DIV_MASK (0x3 << PLLCX_MISC_FILT_DIV_SHIFT)
#define PLLCX_MISC_ALPHA_SHIFT 18
#define PLLCX_MISC_DIV_LOW_RANGE \
		((0x1 << PLLCX_MISC_SDM_DIV_SHIFT) | \
		(0x1 << PLLCX_MISC_FILT_DIV_SHIFT))
#define PLLCX_MISC_DIV_HIGH_RANGE \
		((0x2 << PLLCX_MISC_SDM_DIV_SHIFT) | \
		(0x2 << PLLCX_MISC_FILT_DIV_SHIFT))
#define PLLCX_MISC_COEF_LOW_RANGE \
		((0x14 << PLLCX_MISC_KA_SHIFT) | (0x38 << PLLCX_MISC_KB_SHIFT))
#define PLLCX_MISC_KA_SHIFT 2
#define PLLCX_MISC_KB_SHIFT 9
#define PLLCX_MISC_DEFAULT (PLLCX_MISC_COEF_LOW_RANGE | \
			    (0x19 << PLLCX_MISC_ALPHA_SHIFT) | \
			    PLLCX_MISC_DIV_LOW_RANGE | \
			    PLLCX_MISC_RESET)
#define PLLCX_MISC1_DEFAULT 0x000d2308
#define PLLCX_MISC2_DEFAULT 0x30211200
#define PLLCX_MISC3_DEFAULT 0x200

#define PMC_SATA_PWRGT 0x1ac
#define PMC_SATA_PWRGT_PLLE_IDDQ_VALUE BIT(5)
#define PMC_SATA_PWRGT_PLLE_IDDQ_SWCTL BIT(4)

#define PLLSS_MISC_KCP		0
#define PLLSS_MISC_KVCO		0
#define PLLSS_MISC_SETUP	0
#define PLLSS_EN_SDM		0
#define PLLSS_EN_SSC		0
#define PLLSS_EN_DITHER2	0
#define PLLSS_EN_DITHER		1
#define PLLSS_SDM_RESET		0
#define PLLSS_CLAMP		0
#define PLLSS_SDM_SSC_MAX	0
#define PLLSS_SDM_SSC_MIN	0
#define PLLSS_SDM_SSC_STEP	0
#define PLLSS_SDM_DIN		0
#define PLLSS_MISC_DEFAULT ((PLLSS_MISC_KCP << 25) | \
			    (PLLSS_MISC_KVCO << 24) | \
			    PLLSS_MISC_SETUP)
#define PLLSS_CFG_DEFAULT ((PLLSS_EN_SDM << 31) | \
			   (PLLSS_EN_SSC << 30) | \
			   (PLLSS_EN_DITHER2 << 29) | \
			   (PLLSS_EN_DITHER << 28) | \
			   (PLLSS_SDM_RESET) << 27 | \
			   (PLLSS_CLAMP << 22))
#define PLLSS_CTRL1_DEFAULT \
			((PLLSS_SDM_SSC_MAX << 16) | PLLSS_SDM_SSC_MIN)
#define PLLSS_CTRL2_DEFAULT \
			((PLLSS_SDM_SSC_STEP << 16) | PLLSS_SDM_DIN)
#define PLLSS_LOCK_OVERRIDE	BIT(24)
#define PLLSS_REF_SRC_SEL_SHIFT	25
#define PLLSS_REF_SRC_SEL_MASK	(3 << PLLSS_REF_SRC_SEL_SHIFT)

#define pll_readl(offset, p) readl_relaxed(p->clk_base + offset)
#define pll_readl_base(p) pll_readl(p->params->base_reg, p)
#define pll_readl_misc(p) pll_readl(p->params->misc_reg, p)
#define pll_override_readl(offset, p) readl_relaxed(p->pmc + offset)

#define pll_writel(val, offset, p) writel_relaxed(val, p->clk_base + offset)
#define pll_writel_base(val, p) pll_writel(val, p->params->base_reg, p)
#define pll_writel_misc(val, p) pll_writel(val, p->params->misc_reg, p)
#define pll_override_writel(val, offset, p) writel(val, p->pmc + offset)

#define mask(w) ((1 << (w)) - 1)
#define divm_mask(p) mask(p->params->div_nmp->divm_width)
#define divn_mask(p) mask(p->params->div_nmp->divn_width)
#define divp_mask(p) (p->params->flags & TEGRA_PLLU ? PLLU_POST_DIVP_MASK :\
		      mask(p->params->div_nmp->divp_width))

#define divm_max(p) (divm_mask(p))
#define divn_max(p) (divn_mask(p))
#define divp_max(p) (1 << (divp_mask(p)))

static struct div_nmp default_nmp = {
	.divn_shift = PLL_BASE_DIVN_SHIFT,
	.divn_width = PLL_BASE_DIVN_WIDTH,
	.divm_shift = PLL_BASE_DIVM_SHIFT,
	.divm_width = PLL_BASE_DIVM_WIDTH,
	.divp_shift = PLL_BASE_DIVP_SHIFT,
	.divp_width = PLL_BASE_DIVP_WIDTH,
};

static void clk_pll_enable_lock(struct tegra_clk_pll *pll)
{
	u32 val;

	if (!(pll->params->flags & TEGRA_PLL_USE_LOCK))
		return;

	if (!(pll->params->flags & TEGRA_PLL_HAS_LOCK_ENABLE))
		return;

	val = pll_readl_misc(pll);
	val |= BIT(pll->params->lock_enable_bit_idx);
	pll_writel_misc(val, pll);
}

static int clk_pll_wait_for_lock(struct tegra_clk_pll *pll)
{
	int i;
	u32 val, lock_mask;
	void __iomem *lock_addr;

	if (!(pll->params->flags & TEGRA_PLL_USE_LOCK)) {
		udelay(pll->params->lock_delay);
		return 0;
	}

	lock_addr = pll->clk_base;
	if (pll->params->flags & TEGRA_PLL_LOCK_MISC)
		lock_addr += pll->params->misc_reg;
	else
		lock_addr += pll->params->base_reg;

	lock_mask = pll->params->lock_mask;

	for (i = 0; i < pll->params->lock_delay; i++) {
		val = readl_relaxed(lock_addr);
		if ((val & lock_mask) == lock_mask) {
			udelay(PLL_POST_LOCK_DELAY);
			return 0;
		}
		udelay(2); /* timeout = 2 * lock time */
	}

	pr_err("%s: Timed out waiting for pll %s lock\n", __func__,
	       __clk_get_name(pll->hw.clk));

	return -1;
}

static int clk_pll_is_enabled(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;

	if (pll->params->flags & TEGRA_PLLM) {
		val = readl_relaxed(pll->pmc + PMC_PLLP_WB0_OVERRIDE);
		if (val & PMC_PLLP_WB0_OVERRIDE_PLLM_OVERRIDE)
			return val & PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE ? 1 : 0;
	}

	val = pll_readl_base(pll);

	return val & PLL_BASE_ENABLE ? 1 : 0;
}

static void _clk_pll_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;

	clk_pll_enable_lock(pll);

	val = pll_readl_base(pll);
	if (pll->params->flags & TEGRA_PLL_BYPASS)
		val &= ~PLL_BASE_BYPASS;
	val |= PLL_BASE_ENABLE;
	pll_writel_base(val, pll);

	if (pll->params->flags & TEGRA_PLLM) {
		val = readl_relaxed(pll->pmc + PMC_PLLP_WB0_OVERRIDE);
		val |= PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE;
		writel_relaxed(val, pll->pmc + PMC_PLLP_WB0_OVERRIDE);
	}
}

static void _clk_pll_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;

	val = pll_readl_base(pll);
	if (pll->params->flags & TEGRA_PLL_BYPASS)
		val &= ~PLL_BASE_BYPASS;
	val &= ~PLL_BASE_ENABLE;
	pll_writel_base(val, pll);

	if (pll->params->flags & TEGRA_PLLM) {
		val = readl_relaxed(pll->pmc + PMC_PLLP_WB0_OVERRIDE);
		val &= ~PMC_PLLP_WB0_OVERRIDE_PLLM_ENABLE;
		writel_relaxed(val, pll->pmc + PMC_PLLP_WB0_OVERRIDE);
	}
}

static int clk_pll_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;
	int ret;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pll_enable(hw);

	ret = clk_pll_wait_for_lock(pll);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static void clk_pll_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pll_disable(hw);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static int _p_div_to_hw(struct clk_hw *hw, u8 p_div)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct pdiv_map *p_tohw = pll->params->pdiv_tohw;

	if (p_tohw) {
		while (p_tohw->pdiv) {
			if (p_div <= p_tohw->pdiv)
				return p_tohw->hw_val;
			p_tohw++;
		}
		return -EINVAL;
	}
	return -EINVAL;
}

static int _hw_to_p_div(struct clk_hw *hw, u8 p_div_hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct pdiv_map *p_tohw = pll->params->pdiv_tohw;

	if (p_tohw) {
		while (p_tohw->pdiv) {
			if (p_div_hw == p_tohw->hw_val)
				return p_tohw->pdiv;
			p_tohw++;
		}
		return -EINVAL;
	}

	return 1 << p_div_hw;
}

static int _get_table_rate(struct clk_hw *hw,
			   struct tegra_clk_pll_freq_table *cfg,
			   unsigned long rate, unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table *sel;

	for (sel = pll->params->freq_table; sel->input_rate != 0; sel++)
		if (sel->input_rate == parent_rate &&
		    sel->output_rate == rate)
			break;

	if (sel->input_rate == 0)
		return -EINVAL;

	cfg->input_rate = sel->input_rate;
	cfg->output_rate = sel->output_rate;
	cfg->m = sel->m;
	cfg->n = sel->n;
	cfg->p = sel->p;
	cfg->cpcon = sel->cpcon;

	return 0;
}

static int _calc_rate(struct clk_hw *hw, struct tegra_clk_pll_freq_table *cfg,
		      unsigned long rate, unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long cfreq;
	u32 p_div = 0;
	int ret;

	switch (parent_rate) {
	case 12000000:
	case 26000000:
		cfreq = (rate <= 1000000 * 1000) ? 1000000 : 2000000;
		break;
	case 13000000:
		cfreq = (rate <= 1000000 * 1000) ? 1000000 : 2600000;
		break;
	case 16800000:
	case 19200000:
		cfreq = (rate <= 1200000 * 1000) ? 1200000 : 2400000;
		break;
	case 9600000:
	case 28800000:
		/*
		 * PLL_P_OUT1 rate is not listed in PLLA table
		 */
		cfreq = parent_rate/(parent_rate/1000000);
		break;
	default:
		pr_err("%s Unexpected reference rate %lu\n",
		       __func__, parent_rate);
		BUG();
	}

	/* Raise VCO to guarantee 0.5% accuracy */
	for (cfg->output_rate = rate; cfg->output_rate < 200 * cfreq;
	     cfg->output_rate <<= 1)
		p_div++;

	cfg->m = parent_rate / cfreq;
	cfg->n = cfg->output_rate / cfreq;
	cfg->cpcon = OUT_OF_TABLE_CPCON;

	if (cfg->m > divm_max(pll) || cfg->n > divn_max(pll) ||
	    (1 << p_div) > divp_max(pll)
	    || cfg->output_rate > pll->params->vco_max) {
		return -EINVAL;
	}

	cfg->output_rate >>= p_div;

	if (pll->params->pdiv_tohw) {
		ret = _p_div_to_hw(hw, 1 << p_div);
		if (ret < 0)
			return ret;
		else
			cfg->p = ret;
	} else
		cfg->p = p_div;

	return 0;
}

static void _update_pll_mnp(struct tegra_clk_pll *pll,
			    struct tegra_clk_pll_freq_table *cfg)
{
	u32 val;
	struct tegra_clk_pll_params *params = pll->params;
	struct div_nmp *div_nmp = params->div_nmp;

	if ((params->flags & TEGRA_PLLM) &&
		(pll_override_readl(PMC_PLLP_WB0_OVERRIDE, pll) &
			PMC_PLLP_WB0_OVERRIDE_PLLM_OVERRIDE)) {
		val = pll_override_readl(params->pmc_divp_reg, pll);
		val &= ~(divp_mask(pll) << div_nmp->override_divp_shift);
		val |= cfg->p << div_nmp->override_divp_shift;
		pll_override_writel(val, params->pmc_divp_reg, pll);

		val = pll_override_readl(params->pmc_divnm_reg, pll);
		val &= ~(divm_mask(pll) << div_nmp->override_divm_shift) |
			~(divn_mask(pll) << div_nmp->override_divn_shift);
		val |= (cfg->m << div_nmp->override_divm_shift) |
			(cfg->n << div_nmp->override_divn_shift);
		pll_override_writel(val, params->pmc_divnm_reg, pll);
	} else {
		val = pll_readl_base(pll);

		val &= ~((divm_mask(pll) << div_nmp->divm_shift) |
		 (divn_mask(pll) << div_nmp->divn_shift) |
		 (divp_mask(pll) << div_nmp->divp_shift));

		val |= ((cfg->m << div_nmp->divm_shift) |
			(cfg->n << div_nmp->divn_shift) |
			(cfg->p << div_nmp->divp_shift));

		pll_writel_base(val, pll);
	}
}

static void _get_pll_mnp(struct tegra_clk_pll *pll,
			 struct tegra_clk_pll_freq_table *cfg)
{
	u32 val;
	struct tegra_clk_pll_params *params = pll->params;
	struct div_nmp *div_nmp = params->div_nmp;

	if ((params->flags & TEGRA_PLLM) &&
		(pll_override_readl(PMC_PLLP_WB0_OVERRIDE, pll) &
			PMC_PLLP_WB0_OVERRIDE_PLLM_OVERRIDE)) {
		val = pll_override_readl(params->pmc_divp_reg, pll);
		cfg->p = (val >> div_nmp->override_divp_shift) & divp_mask(pll);

		val = pll_override_readl(params->pmc_divnm_reg, pll);
		cfg->m = (val >> div_nmp->override_divm_shift) & divm_mask(pll);
		cfg->n = (val >> div_nmp->override_divn_shift) & divn_mask(pll);
	}  else {
		val = pll_readl_base(pll);

		cfg->m = (val >> div_nmp->divm_shift) & divm_mask(pll);
		cfg->n = (val >> div_nmp->divn_shift) & divn_mask(pll);
		cfg->p = (val >> div_nmp->divp_shift) & divp_mask(pll);
	}
}

static void _update_pll_cpcon(struct tegra_clk_pll *pll,
			      struct tegra_clk_pll_freq_table *cfg,
			      unsigned long rate)
{
	u32 val;

	val = pll_readl_misc(pll);

	val &= ~(PLL_MISC_CPCON_MASK << PLL_MISC_CPCON_SHIFT);
	val |= cfg->cpcon << PLL_MISC_CPCON_SHIFT;

	if (pll->params->flags & TEGRA_PLL_SET_LFCON) {
		val &= ~(PLL_MISC_LFCON_MASK << PLL_MISC_LFCON_SHIFT);
		if (cfg->n >= PLLDU_LFCON_SET_DIVN)
			val |= 1 << PLL_MISC_LFCON_SHIFT;
	} else if (pll->params->flags & TEGRA_PLL_SET_DCCON) {
		val &= ~(1 << PLL_MISC_DCCON_SHIFT);
		if (rate >= (pll->params->vco_max >> 1))
			val |= 1 << PLL_MISC_DCCON_SHIFT;
	}

	pll_writel_misc(val, pll);
}

static int _program_pll(struct clk_hw *hw, struct tegra_clk_pll_freq_table *cfg,
			unsigned long rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	int state, ret = 0;

	state = clk_pll_is_enabled(hw);

	if (state)
		_clk_pll_disable(hw);

	_update_pll_mnp(pll, cfg);

	if (pll->params->flags & TEGRA_PLL_HAS_CPCON)
		_update_pll_cpcon(pll, cfg, rate);

	if (state) {
		_clk_pll_enable(hw);
		ret = clk_pll_wait_for_lock(pll);
	}

	return ret;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table cfg, old_cfg;
	unsigned long flags = 0;
	int ret = 0;

	if (pll->params->flags & TEGRA_PLL_FIXED) {
		if (rate != pll->params->fixed_rate) {
			pr_err("%s: Can not change %s fixed rate %lu to %lu\n",
				__func__, __clk_get_name(hw->clk),
				pll->params->fixed_rate, rate);
			return -EINVAL;
		}
		return 0;
	}

	if (_get_table_rate(hw, &cfg, rate, parent_rate) &&
	    _calc_rate(hw, &cfg, rate, parent_rate)) {
		pr_err("%s: Failed to set %s rate %lu\n", __func__,
		       __clk_get_name(hw->clk), rate);
		WARN_ON(1);
		return -EINVAL;
	}
	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_get_pll_mnp(pll, &old_cfg);

	if (old_cfg.m != cfg.m || old_cfg.n != cfg.n || old_cfg.p != cfg.p)
		ret = _program_pll(hw, &cfg, rate);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long *prate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table cfg;

	if (pll->params->flags & TEGRA_PLL_FIXED)
		return pll->params->fixed_rate;

	/* PLLM is used for memory; we do not change rate */
	if (pll->params->flags & TEGRA_PLLM)
		return __clk_get_rate(hw->clk);

	if (_get_table_rate(hw, &cfg, rate, *prate) &&
	    _calc_rate(hw, &cfg, rate, *prate))
		return -EINVAL;

	return cfg.output_rate;
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table cfg;
	u32 val;
	u64 rate = parent_rate;
	int pdiv;

	val = pll_readl_base(pll);

	if ((pll->params->flags & TEGRA_PLL_BYPASS) && (val & PLL_BASE_BYPASS))
		return parent_rate;

	if ((pll->params->flags & TEGRA_PLL_FIXED) &&
			!(val & PLL_BASE_OVERRIDE)) {
		struct tegra_clk_pll_freq_table sel;
		if (_get_table_rate(hw, &sel, pll->params->fixed_rate,
					parent_rate)) {
			pr_err("Clock %s has unknown fixed frequency\n",
			       __clk_get_name(hw->clk));
			BUG();
		}
		return pll->params->fixed_rate;
	}

	_get_pll_mnp(pll, &cfg);

	pdiv = _hw_to_p_div(hw, cfg.p);
	if (pdiv < 0) {
		WARN_ON(1);
		pdiv = 1;
	}

	cfg.m *= pdiv;

	rate *= cfg.n;
	do_div(rate, cfg.m);

	return rate;
}

static int clk_plle_training(struct tegra_clk_pll *pll)
{
	u32 val;
	unsigned long timeout;

	if (!pll->pmc)
		return -ENOSYS;

	/*
	 * PLLE is already disabled, and setup cleared;
	 * create falling edge on PLLE IDDQ input.
	 */
	val = readl(pll->pmc + PMC_SATA_PWRGT);
	val |= PMC_SATA_PWRGT_PLLE_IDDQ_VALUE;
	writel(val, pll->pmc + PMC_SATA_PWRGT);

	val = readl(pll->pmc + PMC_SATA_PWRGT);
	val |= PMC_SATA_PWRGT_PLLE_IDDQ_SWCTL;
	writel(val, pll->pmc + PMC_SATA_PWRGT);

	val = readl(pll->pmc + PMC_SATA_PWRGT);
	val &= ~PMC_SATA_PWRGT_PLLE_IDDQ_VALUE;
	writel(val, pll->pmc + PMC_SATA_PWRGT);

	val = pll_readl_misc(pll);

	timeout = jiffies + msecs_to_jiffies(100);
	while (1) {
		val = pll_readl_misc(pll);
		if (val & PLLE_MISC_READY)
			break;
		if (time_after(jiffies, timeout)) {
			pr_err("%s: timeout waiting for PLLE\n", __func__);
			return -EBUSY;
		}
		udelay(300);
	}

	return 0;
}

static int clk_plle_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long input_rate = clk_get_rate(clk_get_parent(hw->clk));
	struct tegra_clk_pll_freq_table sel;
	u32 val;
	int err;

	if (_get_table_rate(hw, &sel, pll->params->fixed_rate, input_rate))
		return -EINVAL;

	clk_pll_disable(hw);

	val = pll_readl_misc(pll);
	val &= ~(PLLE_MISC_LOCK_ENABLE | PLLE_MISC_SETUP_MASK);
	pll_writel_misc(val, pll);

	val = pll_readl_misc(pll);
	if (!(val & PLLE_MISC_READY)) {
		err = clk_plle_training(pll);
		if (err)
			return err;
	}

	if (pll->params->flags & TEGRA_PLLE_CONFIGURE) {
		/* configure dividers */
		val = pll_readl_base(pll);
		val &= ~(divm_mask(pll) | divn_mask(pll) | divp_mask(pll));
		val &= ~(PLLE_BASE_DIVCML_WIDTH << PLLE_BASE_DIVCML_SHIFT);
		val |= sel.m << pll->params->div_nmp->divm_shift;
		val |= sel.n << pll->params->div_nmp->divn_shift;
		val |= sel.p << pll->params->div_nmp->divp_shift;
		val |= sel.cpcon << PLLE_BASE_DIVCML_SHIFT;
		pll_writel_base(val, pll);
	}

	val = pll_readl_misc(pll);
	val |= PLLE_MISC_SETUP_VALUE;
	val |= PLLE_MISC_LOCK_ENABLE;
	pll_writel_misc(val, pll);

	val = readl(pll->clk_base + PLLE_SS_CTRL);
	val |= PLLE_SS_DISABLE;
	writel(val, pll->clk_base + PLLE_SS_CTRL);

	val |= pll_readl_base(pll);
	val |= (PLL_BASE_BYPASS | PLL_BASE_ENABLE);
	pll_writel_base(val, pll);

	clk_pll_wait_for_lock(pll);

	return 0;
}

static unsigned long clk_plle_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val = pll_readl_base(pll);
	u32 divn = 0, divm = 0, divp = 0;
	u64 rate = parent_rate;

	divp = (val >> pll->params->div_nmp->divp_shift) & (divp_mask(pll));
	divn = (val >> pll->params->div_nmp->divn_shift) & (divn_mask(pll));
	divm = (val >> pll->params->div_nmp->divm_shift) & (divm_mask(pll));
	divm *= divp;

	rate *= divn;
	do_div(rate, divm);
	return rate;
}

const struct clk_ops tegra_clk_pll_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};

const struct clk_ops tegra_clk_plle_ops = {
	.recalc_rate = clk_plle_recalc_rate,
	.is_enabled = clk_pll_is_enabled,
	.disable = clk_pll_disable,
	.enable = clk_plle_enable,
};

#if defined(CONFIG_ARCH_TEGRA_114_SOC) || defined(CONFIG_ARCH_TEGRA_124_SOC)

static int _pll_fixed_mdiv(struct tegra_clk_pll_params *pll_params,
			   unsigned long parent_rate)
{
	if (parent_rate > pll_params->cf_max)
		return 2;
	else
		return 1;
}

static unsigned long _clip_vco_min(unsigned long vco_min,
				   unsigned long parent_rate)
{
	return DIV_ROUND_UP(vco_min, parent_rate) * parent_rate;
}

static int _setup_dynamic_ramp(struct tegra_clk_pll_params *pll_params,
			       void __iomem *clk_base,
			       unsigned long parent_rate)
{
	u32 val;
	u32 step_a, step_b;

	switch (parent_rate) {
	case 12000000:
	case 13000000:
	case 26000000:
		step_a = 0x2B;
		step_b = 0x0B;
		break;
	case 16800000:
		step_a = 0x1A;
		step_b = 0x09;
		break;
	case 19200000:
		step_a = 0x12;
		step_b = 0x08;
		break;
	default:
		pr_err("%s: Unexpected reference rate %lu\n",
			__func__, parent_rate);
		WARN_ON(1);
		return -EINVAL;
	}

	val = step_a << pll_params->stepa_shift;
	val |= step_b << pll_params->stepb_shift;
	writel_relaxed(val, clk_base + pll_params->dyn_ramp_reg);

	return 0;
}

static int clk_pll_iddq_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;

	u32 val;
	int ret;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	val = pll_readl(pll->params->iddq_reg, pll);
	val &= ~BIT(pll->params->iddq_bit_idx);
	pll_writel(val, pll->params->iddq_reg, pll);
	udelay(2);

	_clk_pll_enable(hw);

	ret = clk_pll_wait_for_lock(pll);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return 0;
}

static void clk_pll_iddq_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;
	u32 val;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pll_disable(hw);

	val = pll_readl(pll->params->iddq_reg, pll);
	val |= BIT(pll->params->iddq_bit_idx);
	pll_writel(val, pll->params->iddq_reg, pll);
	udelay(2);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static int _calc_dynamic_ramp_rate(struct clk_hw *hw,
				struct tegra_clk_pll_freq_table *cfg,
				unsigned long rate, unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned int p;
	int p_div;

	if (!rate)
		return -EINVAL;

	p = DIV_ROUND_UP(pll->params->vco_min, rate);
	cfg->m = _pll_fixed_mdiv(pll->params, parent_rate);
	cfg->output_rate = rate * p;
	cfg->n = cfg->output_rate * cfg->m / parent_rate;

	p_div = _p_div_to_hw(hw, p);
	if (p_div < 0)
		return p_div;
	else
		cfg->p = p_div;

	if (cfg->n > divn_max(pll) || cfg->output_rate > pll->params->vco_max)
		return -EINVAL;

	return 0;
}

static int _pll_ramp_calc_pll(struct clk_hw *hw,
			      struct tegra_clk_pll_freq_table *cfg,
			      unsigned long rate, unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	int err = 0, p_div;

	err = _get_table_rate(hw, cfg, rate, parent_rate);
	if (err < 0)
		err = _calc_dynamic_ramp_rate(hw, cfg, rate, parent_rate);
	else {
		if (cfg->m != _pll_fixed_mdiv(pll->params, parent_rate)) {
			WARN_ON(1);
			err = -EINVAL;
			goto out;
		}
		p_div = _p_div_to_hw(hw, cfg->p);
		if (p_div < 0)
			return p_div;
		else
			cfg->p = p_div;
	}

	if (cfg->p >  pll->params->max_p)
		err = -EINVAL;

out:
	return err;
}

static int clk_pllxc_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table cfg, old_cfg;
	unsigned long flags = 0;
	int ret = 0;

	ret = _pll_ramp_calc_pll(hw, &cfg, rate, parent_rate);
	if (ret < 0)
		return ret;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_get_pll_mnp(pll, &old_cfg);

	if (old_cfg.m != cfg.m || old_cfg.n != cfg.n || old_cfg.p != cfg.p)
		ret = _program_pll(hw, &cfg, rate);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static long clk_pll_ramp_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct tegra_clk_pll_freq_table cfg;
	int ret = 0, p_div;
	u64 output_rate = *prate;

	ret = _pll_ramp_calc_pll(hw, &cfg, rate, *prate);
	if (ret < 0)
		return ret;

	p_div = _hw_to_p_div(hw, cfg.p);
	if (p_div < 0)
		return p_div;

	output_rate *= cfg.n;
	do_div(output_rate, cfg.m * p_div);

	return output_rate;
}

static int clk_pllm_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct tegra_clk_pll_freq_table cfg;
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;
	int state, ret = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	state = clk_pll_is_enabled(hw);
	if (state) {
		if (rate != clk_get_rate(hw->clk)) {
			pr_err("%s: Cannot change active PLLM\n", __func__);
			ret = -EINVAL;
			goto out;
		}
		goto out;
	}

	ret = _pll_ramp_calc_pll(hw, &cfg, rate, parent_rate);
	if (ret < 0)
		goto out;

	_update_pll_mnp(pll, &cfg);

out:
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static void _pllcx_strobe(struct tegra_clk_pll *pll)
{
	u32 val;

	val = pll_readl_misc(pll);
	val |= PLLCX_MISC_STROBE;
	pll_writel_misc(val, pll);
	udelay(2);

	val &= ~PLLCX_MISC_STROBE;
	pll_writel_misc(val, pll);
}

static int clk_pllc_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;
	int ret = 0;
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pll_enable(hw);
	udelay(2);

	val = pll_readl_misc(pll);
	val &= ~PLLCX_MISC_RESET;
	pll_writel_misc(val, pll);
	udelay(2);

	_pllcx_strobe(pll);

	ret = clk_pll_wait_for_lock(pll);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static void _clk_pllc_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u32 val;

	_clk_pll_disable(hw);

	val = pll_readl_misc(pll);
	val |= PLLCX_MISC_RESET;
	pll_writel_misc(val, pll);
	udelay(2);
}

static void clk_pllc_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pllc_disable(hw);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static int _pllcx_update_dynamic_coef(struct tegra_clk_pll *pll,
					unsigned long input_rate, u32 n)
{
	u32 val, n_threshold;

	switch (input_rate) {
	case 12000000:
		n_threshold = 70;
		break;
	case 13000000:
	case 26000000:
		n_threshold = 71;
		break;
	case 16800000:
		n_threshold = 55;
		break;
	case 19200000:
		n_threshold = 48;
		break;
	default:
		pr_err("%s: Unexpected reference rate %lu\n",
			__func__, input_rate);
		return -EINVAL;
	}

	val = pll_readl_misc(pll);
	val &= ~(PLLCX_MISC_SDM_DIV_MASK | PLLCX_MISC_FILT_DIV_MASK);
	val |= n <= n_threshold ?
		PLLCX_MISC_DIV_LOW_RANGE : PLLCX_MISC_DIV_HIGH_RANGE;
	pll_writel_misc(val, pll);

	return 0;
}

static int clk_pllc_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct tegra_clk_pll_freq_table cfg, old_cfg;
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;
	int state, ret = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	ret = _pll_ramp_calc_pll(hw, &cfg, rate, parent_rate);
	if (ret < 0)
		goto out;

	_get_pll_mnp(pll, &old_cfg);

	if (cfg.m != old_cfg.m) {
		WARN_ON(1);
		goto out;
	}

	if (old_cfg.n == cfg.n && old_cfg.p == cfg.p)
		goto out;

	state = clk_pll_is_enabled(hw);
	if (state)
		_clk_pllc_disable(hw);

	ret = _pllcx_update_dynamic_coef(pll, parent_rate, cfg.n);
	if (ret < 0)
		goto out;

	_update_pll_mnp(pll, &cfg);

	if (state)
		ret = clk_pllc_enable(hw);

out:
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static long _pllre_calc_rate(struct tegra_clk_pll *pll,
			     struct tegra_clk_pll_freq_table *cfg,
			     unsigned long rate, unsigned long parent_rate)
{
	u16 m, n;
	u64 output_rate = parent_rate;

	m = _pll_fixed_mdiv(pll->params, parent_rate);
	n = rate * m / parent_rate;

	output_rate *= n;
	do_div(output_rate, m);

	if (cfg) {
		cfg->m = m;
		cfg->n = n;
	}

	return output_rate;
}
static int clk_pllre_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct tegra_clk_pll_freq_table cfg, old_cfg;
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;
	int state, ret = 0;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_pllre_calc_rate(pll, &cfg, rate, parent_rate);
	_get_pll_mnp(pll, &old_cfg);
	cfg.p = old_cfg.p;

	if (cfg.m != old_cfg.m || cfg.n != old_cfg.n) {
		state = clk_pll_is_enabled(hw);
		if (state)
			_clk_pll_disable(hw);

		_update_pll_mnp(pll, &cfg);

		if (state) {
			_clk_pll_enable(hw);
			ret = clk_pll_wait_for_lock(pll);
		}
	}

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static unsigned long clk_pllre_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct tegra_clk_pll_freq_table cfg;
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	u64 rate = parent_rate;

	_get_pll_mnp(pll, &cfg);

	rate *= cfg.n;
	do_div(rate, cfg.m);

	return rate;
}

static long clk_pllre_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *prate)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);

	return _pllre_calc_rate(pll, NULL, rate, *prate);
}

static int clk_plle_tegra114_enable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	struct tegra_clk_pll_freq_table sel;
	u32 val;
	int ret;
	unsigned long flags = 0;
	unsigned long input_rate = clk_get_rate(clk_get_parent(hw->clk));

	if (_get_table_rate(hw, &sel, pll->params->fixed_rate, input_rate))
		return -EINVAL;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	val = pll_readl_base(pll);
	val &= ~BIT(29); /* Disable lock override */
	pll_writel_base(val, pll);

	val = pll_readl(pll->params->aux_reg, pll);
	val |= PLLE_AUX_ENABLE_SWCTL;
	val &= ~PLLE_AUX_SEQ_ENABLE;
	pll_writel(val, pll->params->aux_reg, pll);
	udelay(1);

	val = pll_readl_misc(pll);
	val |= PLLE_MISC_LOCK_ENABLE;
	val |= PLLE_MISC_IDDQ_SW_CTRL;
	val &= ~PLLE_MISC_IDDQ_SW_VALUE;
	val |= PLLE_MISC_PLLE_PTS;
	val |= PLLE_MISC_VREG_BG_CTRL_MASK | PLLE_MISC_VREG_CTRL_MASK;
	pll_writel_misc(val, pll);
	udelay(5);

	val = pll_readl(PLLE_SS_CTRL, pll);
	val |= PLLE_SS_DISABLE;
	pll_writel(val, PLLE_SS_CTRL, pll);

	val = pll_readl_base(pll);
	val &= ~(divm_mask(pll) | divn_mask(pll) | divp_mask(pll));
	val &= ~(PLLE_BASE_DIVCML_WIDTH << PLLE_BASE_DIVCML_SHIFT);
	val |= sel.m << pll->params->div_nmp->divm_shift;
	val |= sel.n << pll->params->div_nmp->divn_shift;
	val |= sel.cpcon << PLLE_BASE_DIVCML_SHIFT;
	pll_writel_base(val, pll);
	udelay(1);

	_clk_pll_enable(hw);
	ret = clk_pll_wait_for_lock(pll);

	if (ret < 0)
		goto out;

	val = pll_readl(PLLE_SS_CTRL, pll);
	val &= ~(PLLE_SS_CNTL_CENTER | PLLE_SS_CNTL_INVERT);
	val &= ~PLLE_SS_COEFFICIENTS_MASK;
	val |= PLLE_SS_COEFFICIENTS_VAL;
	pll_writel(val, PLLE_SS_CTRL, pll);
	val &= ~(PLLE_SS_CNTL_SSC_BYP | PLLE_SS_CNTL_BYPASS_SS);
	pll_writel(val, PLLE_SS_CTRL, pll);
	udelay(1);
	val &= ~PLLE_SS_CNTL_INTERP_RESET;
	pll_writel(val, PLLE_SS_CTRL, pll);
	udelay(1);

	/* TODO: enable hw control of xusb brick pll */

out:
	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);

	return ret;
}

static void clk_plle_tegra114_disable(struct clk_hw *hw)
{
	struct tegra_clk_pll *pll = to_clk_pll(hw);
	unsigned long flags = 0;
	u32 val;

	if (pll->lock)
		spin_lock_irqsave(pll->lock, flags);

	_clk_pll_disable(hw);

	val = pll_readl_misc(pll);
	val |= PLLE_MISC_IDDQ_SW_CTRL | PLLE_MISC_IDDQ_SW_VALUE;
	pll_writel_misc(val, pll);
	udelay(1);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}
#endif

static struct tegra_clk_pll *_tegra_init_pll(void __iomem *clk_base,
		void __iomem *pmc, struct tegra_clk_pll_params *pll_params,
		spinlock_t *lock)
{
	struct tegra_clk_pll *pll;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->clk_base = clk_base;
	pll->pmc = pmc;

	pll->params = pll_params;
	pll->lock = lock;

	if (!pll_params->div_nmp)
		pll_params->div_nmp = &default_nmp;

	return pll;
}

static struct clk *_tegra_clk_register_pll(struct tegra_clk_pll *pll,
		const char *name, const char *parent_name, unsigned long flags,
		const struct clk_ops *ops)
{
	struct clk_init_data init;

	init.name = name;
	init.ops = ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* Data in .init is copied by clk_register(), so stack variable OK */
	pll->hw.init = &init;

	return clk_register(NULL, &pll->hw);
}

struct clk *tegra_clk_register_pll(const char *name, const char *parent_name,
		void __iomem *clk_base, void __iomem *pmc,
		unsigned long flags, struct tegra_clk_pll_params *pll_params,
		spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk;

	pll_params->flags |= TEGRA_PLL_BYPASS;
	pll_params->flags |= TEGRA_PLL_HAS_LOCK_ENABLE;
	pll = _tegra_init_pll(clk_base, pmc, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_pll_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

struct clk *tegra_clk_register_plle(const char *name, const char *parent_name,
		void __iomem *clk_base, void __iomem *pmc,
		unsigned long flags, struct tegra_clk_pll_params *pll_params,
		spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk;

	pll_params->flags |= TEGRA_PLL_LOCK_MISC | TEGRA_PLL_BYPASS;
	pll_params->flags |= TEGRA_PLL_HAS_LOCK_ENABLE;
	pll = _tegra_init_pll(clk_base, pmc, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_plle_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

#if defined(CONFIG_ARCH_TEGRA_114_SOC) || defined(CONFIG_ARCH_TEGRA_124_SOC)
static const struct clk_ops tegra_clk_pllxc_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pll_iddq_enable,
	.disable = clk_pll_iddq_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_ramp_round_rate,
	.set_rate = clk_pllxc_set_rate,
};

static const struct clk_ops tegra_clk_pllm_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pll_iddq_enable,
	.disable = clk_pll_iddq_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_ramp_round_rate,
	.set_rate = clk_pllm_set_rate,
};

static const struct clk_ops tegra_clk_pllc_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pllc_enable,
	.disable = clk_pllc_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_ramp_round_rate,
	.set_rate = clk_pllc_set_rate,
};

static const struct clk_ops tegra_clk_pllre_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pll_iddq_enable,
	.disable = clk_pll_iddq_disable,
	.recalc_rate = clk_pllre_recalc_rate,
	.round_rate = clk_pllre_round_rate,
	.set_rate = clk_pllre_set_rate,
};

static const struct clk_ops tegra_clk_plle_tegra114_ops = {
	.is_enabled =  clk_pll_is_enabled,
	.enable = clk_plle_tegra114_enable,
	.disable = clk_plle_tegra114_disable,
	.recalc_rate = clk_pll_recalc_rate,
};


struct clk *tegra_clk_register_pllxc(const char *name, const char *parent_name,
			  void __iomem *clk_base, void __iomem *pmc,
			  unsigned long flags,
			  struct tegra_clk_pll_params *pll_params,
			  spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk, *parent;
	unsigned long parent_rate;
	int err;
	u32 val, val_iddq;

	parent = __clk_lookup(parent_name);
	if (!parent) {
		WARN(1, "parent clk %s of %s must be registered first\n",
			name, parent_name);
		return ERR_PTR(-EINVAL);
	}

	if (!pll_params->pdiv_tohw)
		return ERR_PTR(-EINVAL);

	parent_rate = __clk_get_rate(parent);

	pll_params->vco_min = _clip_vco_min(pll_params->vco_min, parent_rate);

	err = _setup_dynamic_ramp(pll_params, clk_base, parent_rate);
	if (err)
		return ERR_PTR(err);

	val = readl_relaxed(clk_base + pll_params->base_reg);
	val_iddq = readl_relaxed(clk_base + pll_params->iddq_reg);

	if (val & PLL_BASE_ENABLE)
		WARN_ON(val_iddq & BIT(pll_params->iddq_bit_idx));
	else {
		val_iddq |= BIT(pll_params->iddq_bit_idx);
		writel_relaxed(val_iddq, clk_base + pll_params->iddq_reg);
	}

	pll_params->flags |= TEGRA_PLL_HAS_LOCK_ENABLE;
	pll = _tegra_init_pll(clk_base, pmc, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_pllxc_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

struct clk *tegra_clk_register_pllre(const char *name, const char *parent_name,
			  void __iomem *clk_base, void __iomem *pmc,
			  unsigned long flags,
			  struct tegra_clk_pll_params *pll_params,
			  spinlock_t *lock, unsigned long parent_rate)
{
	u32 val;
	struct tegra_clk_pll *pll;
	struct clk *clk;

	pll_params->flags |= TEGRA_PLL_HAS_LOCK_ENABLE | TEGRA_PLL_LOCK_MISC;

	pll_params->vco_min = _clip_vco_min(pll_params->vco_min, parent_rate);

	pll = _tegra_init_pll(clk_base, pmc, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	/* program minimum rate by default */

	val = pll_readl_base(pll);
	if (val & PLL_BASE_ENABLE)
		WARN_ON(val & pll_params->iddq_bit_idx);
	else {
		int m;

		m = _pll_fixed_mdiv(pll_params, parent_rate);
		val = m << PLL_BASE_DIVM_SHIFT;
		val |= (pll_params->vco_min / parent_rate)
				<< PLL_BASE_DIVN_SHIFT;
		pll_writel_base(val, pll);
	}

	/* disable lock override */

	val = pll_readl_misc(pll);
	val &= ~BIT(29);
	pll_writel_misc(val, pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_pllre_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

struct clk *tegra_clk_register_pllm(const char *name, const char *parent_name,
			  void __iomem *clk_base, void __iomem *pmc,
			  unsigned long flags,
			  struct tegra_clk_pll_params *pll_params,
			  spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk, *parent;
	unsigned long parent_rate;

	if (!pll_params->pdiv_tohw)
		return ERR_PTR(-EINVAL);

	parent = __clk_lookup(parent_name);
	if (!parent) {
		WARN(1, "parent clk %s of %s must be registered first\n",
			name, parent_name);
		return ERR_PTR(-EINVAL);
	}

	parent_rate = __clk_get_rate(parent);

	pll_params->vco_min = _clip_vco_min(pll_params->vco_min, parent_rate);

	pll_params->flags |= TEGRA_PLL_BYPASS;
	pll_params->flags |= TEGRA_PLL_HAS_LOCK_ENABLE;
	pll_params->flags |= TEGRA_PLLM;
	pll = _tegra_init_pll(clk_base, pmc, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_pllm_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

struct clk *tegra_clk_register_pllc(const char *name, const char *parent_name,
			  void __iomem *clk_base, void __iomem *pmc,
			  unsigned long flags,
			  struct tegra_clk_pll_params *pll_params,
			  spinlock_t *lock)
{
	struct clk *parent, *clk;
	struct pdiv_map *p_tohw = pll_params->pdiv_tohw;
	struct tegra_clk_pll *pll;
	struct tegra_clk_pll_freq_table cfg;
	unsigned long parent_rate;

	if (!p_tohw)
		return ERR_PTR(-EINVAL);

	parent = __clk_lookup(parent_name);
	if (!parent) {
		WARN(1, "parent clk %s of %s must be registered first\n",
			name, parent_name);
		return ERR_PTR(-EINVAL);
	}

	parent_rate = __clk_get_rate(parent);

	pll_params->vco_min = _clip_vco_min(pll_params->vco_min, parent_rate);

	pll_params->flags |= TEGRA_PLL_BYPASS;
	pll = _tegra_init_pll(clk_base, pmc, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	/*
	 * Most of PLLC register fields are shadowed, and can not be read
	 * directly from PLL h/w. Hence, actual PLLC boot state is unknown.
	 * Initialize PLL to default state: disabled, reset; shadow registers
	 * loaded with default parameters; dividers are preset for half of
	 * minimum VCO rate (the latter assured that shadowed divider settings
	 * are within supported range).
	 */

	cfg.m = _pll_fixed_mdiv(pll_params, parent_rate);
	cfg.n = cfg.m * pll_params->vco_min / parent_rate;

	while (p_tohw->pdiv) {
		if (p_tohw->pdiv == 2) {
			cfg.p = p_tohw->hw_val;
			break;
		}
		p_tohw++;
	}

	if (!p_tohw->pdiv) {
		WARN_ON(1);
		return ERR_PTR(-EINVAL);
	}

	pll_writel_base(0, pll);
	_update_pll_mnp(pll, &cfg);

	pll_writel_misc(PLLCX_MISC_DEFAULT, pll);
	pll_writel(PLLCX_MISC1_DEFAULT, pll_params->ext_misc_reg[0], pll);
	pll_writel(PLLCX_MISC2_DEFAULT, pll_params->ext_misc_reg[1], pll);
	pll_writel(PLLCX_MISC3_DEFAULT, pll_params->ext_misc_reg[2], pll);

	_pllcx_update_dynamic_coef(pll, parent_rate, cfg.n);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_pllc_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

struct clk *tegra_clk_register_plle_tegra114(const char *name,
				const char *parent_name,
				void __iomem *clk_base, unsigned long flags,
				struct tegra_clk_pll_params *pll_params,
				spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk;
	u32 val, val_aux;

	pll_params->flags |= TEGRA_PLL_HAS_LOCK_ENABLE;
	pll = _tegra_init_pll(clk_base, NULL, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	/* ensure parent is set to pll_re_vco */

	val = pll_readl_base(pll);
	val_aux = pll_readl(pll_params->aux_reg, pll);

	if (val & PLL_BASE_ENABLE) {
		if ((val_aux & PLLE_AUX_PLLRE_SEL) ||
			(val_aux & PLLE_AUX_PLLP_SEL))
			WARN(1, "pll_e enabled with unsupported parent %s\n",
			  (val_aux & PLLE_AUX_PLLP_SEL) ? "pllp_out0" :
					"pll_re_vco");
	} else {
		val_aux &= ~(PLLE_AUX_PLLRE_SEL | PLLE_AUX_PLLP_SEL);
		pll_writel(val_aux, pll_params->aux_reg, pll);
	}

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
				      &tegra_clk_plle_tegra114_ops);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
#endif

#ifdef CONFIG_ARCH_TEGRA_124_SOC
static const struct clk_ops tegra_clk_pllss_ops = {
	.is_enabled = clk_pll_is_enabled,
	.enable = clk_pll_iddq_enable,
	.disable = clk_pll_iddq_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_ramp_round_rate,
	.set_rate = clk_pllxc_set_rate,
};

struct clk *tegra_clk_register_pllss(const char *name, const char *parent_name,
				void __iomem *clk_base, unsigned long flags,
				struct tegra_clk_pll_params *pll_params,
				spinlock_t *lock)
{
	struct tegra_clk_pll *pll;
	struct clk *clk, *parent;
	struct tegra_clk_pll_freq_table cfg;
	unsigned long parent_rate;
	u32 val;
	int i;

	if (!pll_params->div_nmp)
		return ERR_PTR(-EINVAL);

	parent = __clk_lookup(parent_name);
	if (!parent) {
		WARN(1, "parent clk %s of %s must be registered first\n",
			name, parent_name);
		return ERR_PTR(-EINVAL);
	}

	pll_params->flags = TEGRA_PLL_HAS_LOCK_ENABLE | TEGRA_PLL_USE_LOCK;
	pll = _tegra_init_pll(clk_base, NULL, pll_params, lock);
	if (IS_ERR(pll))
		return ERR_CAST(pll);

	val = pll_readl_base(pll);
	val &= ~PLLSS_REF_SRC_SEL_MASK;
	pll_writel_base(val, pll);

	parent_rate = __clk_get_rate(parent);

	pll_params->vco_min = _clip_vco_min(pll_params->vco_min, parent_rate);

	/* initialize PLL to minimum rate */

	cfg.m = _pll_fixed_mdiv(pll_params, parent_rate);
	cfg.n = cfg.m * pll_params->vco_min / parent_rate;

	for (i = 0; pll_params->pdiv_tohw[i].pdiv; i++)
		;
	if (!i) {
		kfree(pll);
		return ERR_PTR(-EINVAL);
	}

	cfg.p = pll_params->pdiv_tohw[i-1].hw_val;

	_update_pll_mnp(pll, &cfg);

	pll_writel_misc(PLLSS_MISC_DEFAULT, pll);
	pll_writel(PLLSS_CFG_DEFAULT, pll_params->ext_misc_reg[0], pll);
	pll_writel(PLLSS_CTRL1_DEFAULT, pll_params->ext_misc_reg[1], pll);
	pll_writel(PLLSS_CTRL1_DEFAULT, pll_params->ext_misc_reg[2], pll);

	val = pll_readl_base(pll);
	if (val & PLL_BASE_ENABLE) {
		if (val & BIT(pll_params->iddq_bit_idx)) {
			WARN(1, "%s is on but IDDQ set\n", name);
			kfree(pll);
			return ERR_PTR(-EINVAL);
		}
	} else
		val |= BIT(pll_params->iddq_bit_idx);

	val &= ~PLLSS_LOCK_OVERRIDE;
	pll_writel_base(val, pll);

	clk = _tegra_clk_register_pll(pll, name, parent_name, flags,
					&tegra_clk_pllss_ops);

	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}
#endif
