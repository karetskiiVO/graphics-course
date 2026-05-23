package modelbaker

import (
	"fmt"
	"io"
)

type vecelem interface {
	~int | ~int8 | ~int16 | ~int32 | ~int64 |
		~uint | ~uint8 | ~uint16 | ~uint32 | ~uint64 | ~uintptr |
		~float32 | ~float64
}

type Vec2[T vecelem] struct {
	X, Y T
}

func (v Vec2[T]) Arr() [2]T {
	return [2]T{
		v.X, v.Y,
	}
}

func (v Vec2[T]) Slice() []T {
	return []T{
		v.X, v.Y,
	}
}

func (c *Vec2[T]) Min(a, b Vec2[T]) *Vec2[T] {
	c.X = min(a.X, b.X)
	c.Y = min(a.Y, b.Y)
	return c
}

func (c *Vec2[T]) Max(a, b Vec2[T]) *Vec2[T] {
	c.X = max(a.X, b.X)
	c.Y = max(a.Y, b.Y)
	return c
}

func NewVec2[T vecelem](coords ...T) (res Vec2[T]) {
	switch len(coords) {
	case 2:
		res.Y = coords[1]
		fallthrough
	case 1:
		res.X = coords[0]
		fallthrough
	case 0:
		return
	default:
		panic(fmt.Sprintf("len(coords:%v) > 2", coords))
	}
}

type Vec3[T vecelem] struct {
	X, Y, Z T
}

func (v Vec3[T]) Arr() [3]T {
	return [3]T{
		v.X, v.Y, v.Z,
	}
}

func (v Vec3[T]) Slice() []T {
	return []T{
		v.X, v.Y, v.Z,
	}
}

func (c *Vec3[T]) Min(a, b Vec3[T]) *Vec3[T] {
	c.X = min(a.X, b.X)
	c.Y = min(a.Y, b.Y)
	c.Z = min(a.Z, b.Z)
	return c
}

func (c *Vec3[T]) Max(a, b Vec3[T]) *Vec3[T] {
	c.X = max(a.X, b.X)
	c.Y = max(a.Y, b.Y)
	c.Z = max(a.Z, b.Z)
	return c
}

func NewVec3[T vecelem](coords ...T) (res Vec3[T]) {
	switch len(coords) {
	case 3:
		res.Z = coords[2]
		fallthrough
	case 2:
		res.Y = coords[1]
		fallthrough
	case 1:
		res.X = coords[0]
		fallthrough
	case 0:
		return
	default:
		panic(fmt.Sprintf("len(coords:%v) > 3", coords))
	}
}

type Vec4[T vecelem] struct {
	X, Y, Z, W T
}

func (v Vec4[T]) Arr() [4]T {
	return [4]T{
		v.X, v.Y, v.Z, v.W,
	}
}

func (v Vec4[T]) Slice() []T {
	return []T{
		v.X, v.Y, v.Z, v.W,
	}
}

func (c *Vec4[T]) Min(a, b Vec4[T]) *Vec4[T] {
	c.X = min(a.X, b.X)
	c.Y = min(a.Y, b.Y)
	c.Z = min(a.Z, b.Z)
	c.W = min(a.W, b.W)
	return c
}

func (c *Vec4[T]) Max(a, b Vec4[T]) *Vec4[T] {
	c.X = max(a.X, b.X)
	c.Y = max(a.Y, b.Y)
	c.Z = max(a.Z, b.Z)
	c.W = max(a.W, b.W)
	return c
}

func NewVec4[T vecelem](coords ...T) (res Vec4[T]) {
	switch len(coords) {
	case 4:
		res.W = coords[3]
		fallthrough
	case 3:
		res.Z = coords[2]
		fallthrough
	case 2:
		res.Y = coords[1]
		fallthrough
	case 1:
		res.X = coords[0]
		fallthrough
	case 0:
		return
	default:
		panic(fmt.Sprintf("len(coords:%v) > 4", coords))
	}
}

type Vec2f = Vec2[float32]
type Vec3f = Vec3[float32]
type Vec4f = Vec4[float32]

type Vertex struct {
	Position Vec3f
	Normal   Vec3f
	TexCoord Vec2f
	Tangent  Vec3f
}

type CompressedVertex struct {
	Position Vec3f
	Norm     Vec3[int8]
	Pad0     [1]byte
	TexCoord Vec2f
	Tangent  Vec4[int8]
	Pad1     [4]byte
}

type emptyReader struct{}

func (emptyReader) Read(b []byte) (int, error) {
	return 0, io.EOF
}
