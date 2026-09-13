export interface TrendSample { ts:number; value:number; breakBefore?:boolean }
export interface TrendSeries { key:string; label:string; unit?:string; color:string; samples:TrendSample[] }
