#ifndef PIXELFORMAT_HH
#define PIXELFORMAT_HH

#include <cstdint>

namespace openmsx {

class PixelFormat
{
public:
	PixelFormat() = default;
	PixelFormat(unsigned bpp_,
	            uint32_t Rmask_, uint8_t Rshift_, uint8_t Rloss_,
	            uint32_t Gmask_, uint8_t Gshift_, uint8_t Gloss_,
	            uint32_t Bmask_, uint8_t Bshift_, uint8_t Bloss_,
	            uint32_t Amask_, uint8_t Ashift_, uint8_t Aloss_)
		: Rmask (Rmask_),  Gmask (Gmask_),  Bmask (Bmask_),  Amask (Amask_)
		, Rshift(Rshift_), Gshift(Gshift_), Bshift(Bshift_), Ashift(Ashift_)
		, Rloss (Rloss_),  Gloss (Gloss_),  Bloss (Bloss_),  Aloss (Aloss_)
		, bpp(bpp_), bytesPerPixel((bpp + 7) / 8) {}

	unsigned getBpp()           const { return bpp; }
	unsigned getBytesPerPixel() const { return bytesPerPixel; }

	unsigned getRmask() const  { return Rmask; }
	unsigned getGmask() const  { return Gmask; }
	unsigned getBmask() const  { return Bmask; }
	unsigned getAmask() const  { return Amask; }

	unsigned getRshift() const { return Rshift; }
	unsigned getGshift() const { return Gshift; }
	unsigned getBshift() const { return Bshift; }
	unsigned getAshift() const { return Ashift; }

	unsigned getRloss() const  { return Rloss; }
	unsigned getGloss() const  { return Gloss; }
	unsigned getBloss() const  { return Bloss; }
	unsigned getAloss() const  { return Aloss; }

	unsigned map(unsigned r, unsigned g, unsigned b) const
	{
		return ((r >> Rloss) << Rshift) |
		       ((g >> Gloss) << Gshift) |
		       ((b >> Bloss) << Bshift) |
		       Amask;
	}

private:
	uint32_t Rmask,  Gmask,  Bmask,  Amask;
	uint8_t  Rshift, Gshift, Bshift, Ashift;
	uint8_t  Rloss,  Gloss,  Bloss,  Aloss;
	uint8_t bpp, bytesPerPixel;
};

} // namespace openmsx

#endif
