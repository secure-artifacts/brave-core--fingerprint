<script setup lang="ts">
interface ScreenshotCallout {
  x: number
  y: number
  title: string
  description: string
}

defineProps<{
  src: string
  alt: string
  caption: string
  callouts: ScreenshotCallout[]
}>()
</script>

<template>
  <figure
    class="annotated-screenshot"
    :class="{'annotated-screenshot--toolbar': src.endsWith('/proxy-toolbar.png')}"
  >
    <div class="annotated-screenshot__image">
      <img :src="src" :alt="alt" loading="lazy" decoding="async">
      <span
        v-for="(callout, index) in callouts"
        :key="`focus-${callout.x}-${callout.y}-${index}`"
        class="annotated-screenshot__focus"
        :style="{left: `${callout.x}%`, top: `${callout.y}%`}"
        aria-hidden="true"
      />
      <span
        v-for="(callout, index) in callouts"
        :key="`${callout.x}-${callout.y}-${index}`"
        class="annotated-screenshot__marker"
        :style="{left: `calc(${callout.x}% - 7%)`, top: `calc(${callout.y}% - 4%)`}"
        aria-hidden="true"
      >
        {{ index + 1 }}
      </span>
    </div>
    <figcaption>
      {{ caption }}
      <a :href="src" target="_blank" rel="noopener">查看大图</a>
    </figcaption>
    <ol class="annotated-screenshot__legend">
      <li v-for="(callout, index) in callouts" :key="callout.title">
        <span class="annotated-screenshot__number">{{ index + 1 }}</span>
        <span>
          <strong>{{ callout.title }}</strong>
          {{ callout.description }}
        </span>
      </li>
    </ol>
  </figure>
</template>
